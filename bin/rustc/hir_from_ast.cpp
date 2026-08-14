#include "hir_from_ast.h"

#include "common.h"
#include "ast_ast.h"
#include "hir_hir.h"
#include "ast_expr.h" // For shortcut in array size handling
#include "hir_expr.h"
#include "settings.h"
#include "ast_crate.h"
#include "wire_board.h"
#include "hir_visitor.h"
#include "hir_expr_ptr.h"
#include "trans_target.h"
#include "hir_item_path.h"
#include "hir_main_bindings.h"
#include "hir_typeck_helpers.h" // monomorph
#include "hir_conv_main_bindings.h"
#include "macro_rules_macro_rules.h"
#include "parse_common.h"
#include "parse_ttstream.h"

#include <std/mem/obj_pool.h>

#include <limits.h>
#include <unordered_set>

struct ImplTraitSource {
    const HIRItemPath* path;
    const HIRGenericParams* paramsOuter;
    const HIRGenericParams* paramsInner = nullptr;

    ImplTraitSource(const HIRItemPath* path, const HIRGenericParams* paramsOuter, const HIRGenericParams* paramsInner = nullptr)
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
};

// All HIR-lowering state, threaded via `this` instead of file-scope globals.
// Every `LowerHIR*` is a method; helper structs hold an `AST2HIR&` back-ref.
struct AST2HIR {
    const WireBoard* mWb = nullptr;
    HIRSimplePath pathSized;
    HIRSimplePath pathPointeeSized;
    HIRSimplePath pathMetadataSized;
    RcString mCoreCrate; // lowering-internal working copy; the canonical value lives in Settings
    RcString mCrateName;
    HIRCrate* mCrate = nullptr;
    const ASTCrate* mAstCrate = nullptr;
    ImplTraitSource mImplTraitSource;

    HIRPublicity LowerHIRVis(const HIRSimplePath& modPath, const ASTVisibility& vis);
    HIRGenericParams LowerHIRGenericParams(const ASTGenericParams& gp, bool* selfIsSized);
    HIRPath LowerHIRPatternPath(const Span& sp, const ASTPath& path, FromASTPathClass pc);
    HIRPattern LowerHIRPattern(const ASTPattern& pat);
    HIRExprPtr LowerHIRExpr(const ::std::shared_ptr<ASTExprNode>& e);
    HIRExprPtr LowerHIRExpr(const ASTExpr& e);
    HIRSimplePath LowerHIRSimplePath(const Span& sp, const ASTPath& path, FromASTPathClass pc, bool allowFinalGeneric = false);
    HIRPathParams LowerHIRPathParams(const Span& sp, const ASTPathParams& srcParams, bool allowAssoc);
    HIRConstGeneric LowerHIRConstGeneric(const ASTExprNode& nodeRef);
    HIRGenericPath LowerHIRGenericPath(const Span& sp, const ASTPath& path, FromASTPathClass pc, bool allowAssoc = false);
    HIRTraitPath LowerHIRTraitPath(const Span& sp, const ASTPath& path, const ASTHigherRankedBounds& hrbs, bool ignoreBounds = false, ASTBoundConstness constness = ASTBoundConstness::Never);
    HIRPath LowerHIRPath(const Span& sp, const ASTPath& path, FromASTPathClass pc);
    HIRTypeRef LowerHIRType(::ASTType* ty);
    HIRTypeAlias LowerHIRTypeAlias(const HIRItemPath& p, const ASTTypeAlias& ta);
    tStructFields LowerHIRStructFields(HIRItemPath path, const HIRGenericParams& params, const ::std::vector<ASTStructItem>& inFields, HIRModule& outMod);
    HIRStruct LowerHIRStruct(const Span& sp, HIRItemPath path, const ASTStruct& ent, const ASTAttributeList& attrs, HIRModule& outMod);
    HIREnum LowerHIREnum(HIRItemPath path, const ASTEnum& ent, const ASTAttributeList& attrs, ::std::function<void(RcString, HIRStruct)> pushStruct, HIRModule& outMod);
    HIRUnion LowerHIRUnion(HIRItemPath path, const ASTUnion& f, const ASTAttributeList& attrs);
    HIRTrait LowerHIRTrait(HIRSimplePath traitPath, const ASTTrait& f, const ASTAttributeList& attrs);
    HIRTraitAlias LowerHIRTraitAlias(const Span& sp, HIRItemPath p, const ASTTraitAlias& f);
    HIRFunction LowerHIRFunction(HIRItemPath p, const ASTAttributeList& attrs, const ASTFunction& f, const HIRTypeData* realSelfType);
    HIRValueItem LowerHIRStatic(HIRItemPath p, const ASTAttributeList& attrs, const ASTStatic& e, const Span& sp, const RcString& name);
    HIRModule LowerHIRModule(const ASTModule& astMod, HIRItemPath path, ::std::vector<HIRSimplePath> traits = {});
    void LowerHIRModuleImpls(const ASTModule& astMod, HIRCrate& hirCrate);
    HIRExprPtr LowerHIRExprNode(const ASTExprNode& e);

    HIRCrate* lowerCrate(const WireBoard& wb, stl::ObjPool* pool, ASTCrate& crate);
};

namespace {
    HIRBoundConstness LowerHIRBoundConstness(ASTBoundConstness v) {
        switch (v) {
            case ASTBoundConstness::Never:
                return HIRBoundConstness::Never;
            case ASTBoundConstness::Always:
                return HIRBoundConstness::Always;
            case ASTBoundConstness::Maybe:
                return HIRBoundConstness::Maybe;
        }
        throw "Invalid bound constness";
    }
}

// --------------------------------------------------------------------
HIRPublicity AST2HIR::LowerHIRVis(const HIRSimplePath& modPath, const ASTVisibility& vis) {
    if (vis.isGlobal()) {
        return HIRPublicity::newGlobal();
    }
    const auto* ap = &vis.visPath();
    return HIRPublicity::newPriv(HIRSimplePath((ap->crate == "" ? mCrateName : ap->crate), ap->nodes));
}

HIRGenericParams AST2HIR::LowerHIRGenericParams(const ASTGenericParams& gp, bool* selfIsSized) {
    HIRGenericParams rv;

    for (const auto& param : gp.mParams) {
        TU_MATCH_HDRA( (param), {)
        TU_ARMA(None, _) {
            }
            TU_ARMA(Lifetime, lftDef) {
            }
            TU_ARMA(Type, tp) {
                rv.types.push_back({tp.name(), LowerHIRType(tp.getDefault()), true});
            }
            TU_ARMA(Value, tp) {
                rv.values.push_back(HIRValueParamDef{tp.name().name, LowerHIRType(tp.type()), tp.defaultValue() ? LowerHIRConstGeneric(tp.defaultValue().node()) : HIRConstGeneric::make_Infer({})});
            }
        }
    }

    for (const auto& bound : gp.bounds) {
        TU_MATCH_HDRA( (bound), {)
        TU_ARMA(None, e) {
            }
            TU_ARMA(Lifetime, e) {
                // Lifetimes are erased in HIR
            }
            TU_ARMA(TypeLifetime, e) {
                // Lifetimes are erased in HIR
            }
            TU_ARMA(IsTrait, e) {
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

                rv.bounds.push_back(HIRGenericBound::make_TraitBound({type, mv$(boundTraitPath), LowerHIRBoundConstness(e.constness)}));

                for (auto& bound : tpBounds) {
                    const auto& name = bound.first;
                    const auto& srcTrait = bound.second.sourceTrait;
                    const auto& params = bound.second.atyParams;
                    for (auto& trait : bound.second.traits) {
                        rv.bounds.push_back(HIRGenericBound::make_TraitBound({mCrate->types.path(HIRPath(type, srcTrait.clone(), name, params.clone()), {}), std::move(trait)}));
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
                rv.bounds.push_back(HIRGenericBound::make_TypeEquality({LowerHIRType(e.type), LowerHIRType(e.replacement)}));
            }
        }
    }

    return rv;
}

HIRPath AST2HIR::LowerHIRPatternPath(const Span& sp, const ASTPath& path, FromASTPathClass pc) {
    if (const auto* be = path.mBindings.type.binding.opt_TypeParameter()) {
        if (be->slot == GENERICSelf) {
            // HACK: Return `<Self>::` (to be expanded later on)
            return HIRPath(mCrate->types.self(), "");
        }
    }
    return LowerHIRPath(sp, path, pc);
}

namespace {
    HIRPatternBinding::Type convertBindingType(ASTPatternBinding::Type pbt) {
        switch (pbt) {
            case ASTPatternBinding::Type::MOVE:
                return HIRPatternBinding::Type::Move;
            case ASTPatternBinding::Type::REF:
                return HIRPatternBinding::Type::Ref;
            case ASTPatternBinding::Type::MUTREF:
                return HIRPatternBinding::Type::MutRef;
        }
        throw "";
    }
}

HIRPattern AST2HIR::LowerHIRPattern(const ASTPattern& pat) {
    TRACE_FUNCTION_F("@" << pat.span() << " pat = " << pat);

    std::vector<HIRPatternBinding> bindings;
    for (const auto& pb : pat.bindings()) {
        bindings.push_back(HIRPatternBinding(pb.isMutable, convertBindingType(pb.mType), pb.mName.name, pb.slot));
    }

    struct H {
        AST2HIR& mCtx;
        explicit H(AST2HIR& ctx) : mCtx(ctx) {}
        ::std::vector<HIRPattern> lowerhirPatternvec(const ::std::vector<ASTPattern>& subPatterns) {
            ::std::vector<HIRPattern> rv;
            for (const auto& sp : subPatterns) {
                rv.push_back(mCtx.LowerHIRPattern(sp));
            }
            return rv;
        }

        HIRCoreType getIntType(const Span& sp, const ::eCoreType ct) {
            switch (ct) {
                case CORETYPE_ANY:
                    return HIRCoreType::Str;

                case CORETYPE_I8:
                    return HIRCoreType::I8;
                case CORETYPE_U8:
                    return HIRCoreType::U8;
                case CORETYPE_I16:
                    return HIRCoreType::I16;
                case CORETYPE_U16:
                    return HIRCoreType::U16;
                case CORETYPE_I32:
                    return HIRCoreType::I32;
                case CORETYPE_U32:
                    return HIRCoreType::U32;
                case CORETYPE_I64:
                    return HIRCoreType::I64;
                case CORETYPE_U64:
                    return HIRCoreType::U64;

                case CORETYPE_INT:
                    return HIRCoreType::Isize;
                case CORETYPE_UINT:
                    return HIRCoreType::Usize;

                case CORETYPE_CHAR:
                    return HIRCoreType::Char;

                case CORETYPE_BOOL:
                    return HIRCoreType::Bool;

                default:
                    BUG(sp, "Unknown type for integer literal in pattern - " << ct);
            }
        }

        HIRCoreType getFloatType(const Span& sp, const ::eCoreType ct) {
            switch (ct) {
                case CORETYPE_ANY:
                    return HIRCoreType::Str;
                case CORETYPE_F16:
                    return HIRCoreType::F16;
                case CORETYPE_F32:
                    return HIRCoreType::F32;
                case CORETYPE_F64:
                    return HIRCoreType::F64;
                case CORETYPE_F128:
                    return HIRCoreType::F128;
                default:
                    BUG(sp, "Unknown type for float literal in pattern - " << ct);
            }
        }

        HIRPattern::Value lowerhirPatternValue(const Span& sp, const ASTPattern::Value& v) {
            TU_MATCH_HDRA((v), {)
            TU_ARMA(Invalid, e) {
                    BUG(sp, "Encountered Invalid value in Pattern");
                }
                TU_ARMA(Integer, e) {
                    return HIRPattern::Value::make_Integer({getIntType(sp, e.type), e.value});
                }
                TU_ARMA(Float, e) {
                    return HIRPattern::Value::make_Float({getFloatType(sp, e.type), e.value});
                }
                TU_ARMA(String, e) {
                    return HIRPattern::Value::make_String(e);
                }
                TU_ARMA(ByteString, e) {
                    return HIRPattern::Value::make_ByteString({e.v});
                }
                TU_ARMA(Named, e) {
                    return HIRPattern::Value::make_Named({mCtx.LowerHIRPatternPath(sp, e, FromASTPathClass::Value), nullptr});
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
        return HIRPattern{mv$(bindings), HIRPattern::Data::make_Any({})};
        TU_ARMA(Box, e)
        return HIRPattern{mv$(bindings), HIRPattern::Data::make_Box({box$(LowerHIRPattern(*e.sub))})};
        TU_ARMA(Ref, e)
        return HIRPattern{mv$(bindings), HIRPattern::Data::make_Ref({(e.mut ? HIRBorrowType::Unique : HIRBorrowType::Shared), box$(LowerHIRPattern(*e.sub))})};
        TU_ARMA(Tuple, e) {
            auto leading = H(*this).lowerhirPatternvec(e.start);
            auto trailing = H(*this).lowerhirPatternvec(e.end);

            if (e.hasWildcard) {
                return HIRPattern(mv$(bindings), HIRPattern::Data::make_SplitTuple({mv$(leading), mv$(trailing)}));
            } else {
                assert(trailing.size() == 0);
                return HIRPattern(mv$(bindings), HIRPattern::Data::make_Tuple({mv$(leading)}));
            }
        }
        ///
        /// Named tuple pattern
        ///
        TU_ARMA(StructTuple, e) {
            auto leading = H(*this).lowerhirPatternvec(e.tupPat.start);
            auto trailing = H(*this).lowerhirPatternvec(e.tupPat.end);

            if (!e.tupPat.hasWildcard) {
                assert(trailing.size() == 0);
            }

            return HIRPattern(
                mv$(bindings),
                HIRPattern::Data::make_PathTuple({
                    LowerHIRPatternPath(pat.span(), e.path, FromASTPathClass::Value),
                    HIRPattern::PathBinding(),
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
            ::std::vector<::std::pair<RcString, HIRPattern>> subPatterns;
            for (const auto& sp : e.subPatterns) {
                subPatterns.push_back(::std::make_pair(sp.name, LowerHIRPattern(sp.pat)));
            }

            // No sub-patterns, no `..`, and the VALUE binding points to an enum variant
            if (e.subPatterns.empty() /*&& !e.is_exhaustive*/) {
                if (/*const auto* pbp =*/e.path.mBindings.value.binding.opt_EnumVar()) {
                    return HIRPattern{
                        mv$(bindings),
                        HIRPattern::Data::make_PathNamed(
                            {LowerHIRGenericPath(pat.span(), e.path, FromASTPathClass::Value),
                             //::HIR::Pattern::PathBinding::make_Enum({ pbp->hir, pbp->idx }),
                             HIRPattern::PathBinding(),
                             mv$(subPatterns),
                             e.isExhaustive}
                        )
                    };
                }
            }

            return HIRPattern(mv$(bindings), HIRPattern::Data::make_PathNamed({LowerHIRPatternPath(pat.span(), e.path, FromASTPathClass::Type), HIRPattern::PathBinding(), mv$(subPatterns), e.isExhaustive}));
        }

        TU_ARMA(Value, e) {
            if (e.end.is_Invalid()) {
                return HIRPattern{mv$(bindings), HIRPattern::Data::make_Value({H(*this).lowerhirPatternValue(pat.span(), e.start)})};
            } else if (e.start.is_Invalid()) {
                return HIRPattern{mv$(bindings), HIRPattern::Data::make_Range({{}, box$(H(*this).lowerhirPatternValue(pat.span(), e.end)), true})};
            } else {
                return HIRPattern{mv$(bindings), HIRPattern::Data::make_Range({box$(H(*this).lowerhirPatternValue(pat.span(), e.start)), box$(H(*this).lowerhirPatternValue(pat.span(), e.end)), true})};
            }
        }
        TU_ARMA(ValueLeftInc, e) {
            if (e.end.is_Invalid()) {
                return HIRPattern{mv$(bindings), HIRPattern::Data::make_Range({box$(H(*this).lowerhirPatternValue(pat.span(), e.start)), {}, false})};
            }
            if (e.start.is_Invalid()) {
                return HIRPattern{mv$(bindings), HIRPattern::Data::make_Range({{}, box$(H(*this).lowerhirPatternValue(pat.span(), e.end)), false})};
            }
            return HIRPattern{mv$(bindings), HIRPattern::Data::make_Range({box$(H(*this).lowerhirPatternValue(pat.span(), e.start)), box$(H(*this).lowerhirPatternValue(pat.span(), e.end)), false})};
        }
        TU_ARMA(Slice, e) {
            ::std::vector<HIRPattern> leading;
            for (const auto& sp : e.subPats) {
                leading.push_back(LowerHIRPattern(sp));
            }
            return HIRPattern{mv$(bindings), HIRPattern::Data::make_Slice({mv$(leading)})};
        }
        TU_ARMA(SplitSlice, e) {
            ::std::vector<HIRPattern> leading;
            for (const auto& sp : e.leading) {
                leading.push_back(LowerHIRPattern(sp));
            }

            ::std::vector<HIRPattern> trailing;
            for (const auto& sp : e.trailing) {
                trailing.push_back(LowerHIRPattern(sp));
            }

            auto extraBind = e.extraBind.isValid() ? HIRPatternBinding(false, convertBindingType(e.extraBind.mType), e.extraBind.mName.name, e.extraBind.slot) : HIRPatternBinding();

            return HIRPattern{mv$(bindings), HIRPattern::Data::make_SplitSlice({mv$(leading), mv$(extraBind), mv$(trailing)})};
        }
        TU_ARMA(Or, e) {
            ::std::vector<HIRPattern> subpats;
            for (const auto& sp : e) {
                subpats.push_back(LowerHIRPattern(sp));
            }
            return HIRPattern{mv$(bindings), HIRPattern::Data::make_Or(mv$(subpats))};
        }
    }
    throw "unreachable";
}

HIRExprPtr AST2HIR::LowerHIRExpr(const ::std::shared_ptr<ASTExprNode>& e) {
    if (e.get()) {
        return LowerHIRExprNode(*e);
    } else {
        return HIRExprPtr();
    }
}

HIRExprPtr AST2HIR::LowerHIRExpr(const ASTExpr& e) {
    if (e.isValid()) {
        return LowerHIRExprNode(e.node());
    } else {
        return HIRExprPtr();
    }
}

HIRSimplePath AST2HIR::LowerHIRSimplePath(const Span& sp, const ASTPath& path, FromASTPathClass pc, bool allowFinalGeneric) {
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
    return HIRSimplePath((ap->crate == "" ? mCrateName : ap->crate), ap->nodes);
}

HIRPathParams AST2HIR::LowerHIRPathParams(const Span& sp, const ASTPathParams& srcParams, bool allowAssoc) {
    HIRPathParams params;

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

    params.types.reserveInit(numTy);
    params.values.reserveInit(numVal);
    for (const auto& param : srcParams.entries) {
        TU_MATCH_HDRA( (param), {)
        TU_ARMA(Null, ty) {
            }
            TU_ARMA(Lifetime, lft) {
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

HIRConstGeneric AST2HIR::LowerHIRConstGeneric(const ASTExprNode& nodeRef) {
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
            return HIRGenericRef(e->mPath.asTrivial(), param.index);
        }
    }
    return std::make_unique<HIRConstGenericUnevaluated>(LowerHIRExprNode(nodeRef));
}

HIRGenericPath AST2HIR::LowerHIRGenericPath(const Span& sp, const ASTPath& path, FromASTPathClass pc, bool allowAssoc) {
    if (const auto* e = path.cls.opt_Absolute()) {
        auto simpepath = LowerHIRSimplePath(sp, path, pc, /*allow_params*/ true);
        HIRPathParams params = LowerHIRPathParams(sp, e->nodes.back().args(), allowAssoc);
        auto rv = HIRGenericPath(mv$(simpepath), mv$(params));
        DEBUG(path << " => " << rv);
        return rv;
    } else {
        if (const auto* e = path.cls.opt_UFCS()) {
            DEBUG(path);
            if (!e->type) {
            }
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

HIRTraitPath AST2HIR::LowerHIRTraitPath(const Span& sp, const ASTPath& path, const ASTHigherRankedBounds& hrbs, bool ignoreBounds /*=false*/, ASTBoundConstness constness /*=Never*/) {
    DEBUG(hrbs << " " << path);
    HIRTraitPath rv{LowerHIRGenericPath(sp, path, FromASTPathClass::Type, /*allow_assoc=*/true), {}, {}, nullptr, LowerHIRBoundConstness(constness)};

    struct H {
        enum class Namespace {
            Type,
            Function,
        };

        AST2HIR& mCtx;
        explicit H(AST2HIR& ctx) : mCtx(ctx) {}
        bool hasItem(const HIRTrait& trait, const RcString& name, Namespace ns) const {
            if (ns == Namespace::Type) {
                return trait.types.find(name) != trait.types.end();
            }
            auto it = trait.values.find(name);
            return it != trait.values.end() && it->second.is_Function();
        }
        bool hasItem(const ASTTrait& trait, const RcString& name, Namespace ns) const {
            for (const auto& item : trait.items()) {
                if (item.name == name && (ns == Namespace::Type ? item.data.is_Type() : item.data.is_Function())) {
                    return true;
                }
            }
            return false;
        }
        HIRGenericPath findSourceTraitHir(const Span& sp, const HIRGenericPath& path, const HIRTrait& trait, const RcString& name, Namespace ns, const Monomorphiser& ms) {
            if (hasItem(trait, name, ns)) {
                return ms.monomorphGenericpath(sp, path, /*allow_infer=*/true);
            }
            auto selfTy = mCtx.mCrate->types.self();
            auto cb = MonomorphStatePtr(mCtx.mCrate->types, selfTy, &path.mParams, nullptr);
            for (const auto& st : trait.allParentTraits) {
                // NOTE: st.m_trait_ptr isn't populated yet
                const auto& t = mCtx.mCrate->getTraitByPath(sp, st.mPath.mPath);

                if (hasItem(t, name, ns)) {
                    // Monomorphse into outer scope, then run the outer monomorph
                    auto p = cb.monomorphGenericpath(sp, st.mPath, /*allow_infer=*/true);
                    return ms.monomorphGenericpath(sp, p, /*allow_infer=*/true);
                }
            }
            return HIRGenericPath();
        }

        HIRGenericPath findSourceTraitAst(const Span& sp, const HIRGenericPath& path, const ASTTrait& trait, const RcString& name, Namespace ns, const Monomorphiser& ms) {
            if (hasItem(trait, name, ns)) {
                return ms.monomorphGenericpath(sp, path, /*allow_infer=*/true);
            }

            auto selfTy = mCtx.mCrate->types.self();
            auto cb = MonomorphStatePtr(mCtx.mCrate->types, selfTy, &path.mParams, nullptr);
            for (const auto& st : trait.supertraits()) {
                auto b = mCtx.LowerHIRTraitPath(sp, *st.ent.path, st.ent.hrbs, true, st.ent.constness);
                ASSERT_BUG(sp, st.ent.path->mBindings.type.binding.is_Trait(), "Not a trait: " << *st.ent.path);
                auto rv = findSourceTrait(sp, b.mPath, st.ent.path->mBindings.type.binding.as_Trait(), name, ns, cb);
                if (rv != HIRGenericPath()) {
                    return ms.monomorphGenericpath(sp, rv, /*allow_infer=*/true);
                }
            }
            return HIRGenericPath();
        }

        HIRGenericPath findSourceTrait(const Span& sp, const HIRGenericPath& path, const ASTPathBindingType& pb, const RcString& name, Namespace ns, const Monomorphiser& ms) {
            TRACE_FUNCTION_F(path);
            if (pb.is_Trait()) {
                const auto& pbe = pb.as_Trait();
                if (pbe.hir) {
                    assert(pbe.hir);
                    return findSourceTraitHir(sp, path, *pbe.hir, name, ns, ms);
                } else if (pbe.trait_) {
                    assert(pbe.trait_);
                    return findSourceTraitAst(sp, path, *pbe.trait_, name, ns, ms);
                } else {
                    BUG(sp, "Unbound path");
                }
            } else if (pb.is_TraitAlias()) {
                const auto& pbe = pb.as_TraitAlias();
                if (pbe.hir) {
                    for (const auto& subTrait : pbe.hir->traits) {
                        auto p = ms.monomorphGenericpath(sp, subTrait.mPath);
                        const auto& t = mCtx.mCrate->getTraitByPath(sp, p.mPath);
                        auto selfTy = mCtx.mCrate->types.self();
                        auto rv = findSourceTraitHir(sp, p, t, name, ns, MonomorphStatePtr(mCtx.mCrate->types, selfTy, &p.mParams, nullptr));
                        if (rv != HIRGenericPath()) {
                            return rv;
                        }
                    }
                    return HIRGenericPath();
                } else if (pbe.trait_) {
                    auto selfTy = mCtx.mCrate->types.self();
                    auto cb = MonomorphStatePtr(mCtx.mCrate->types, selfTy, &path.mParams, nullptr);
                    for (const auto& st : pbe.trait_->traits) {
                        auto b = mCtx.LowerHIRTraitPath(sp, *st.ent.path, st.ent.hrbs, true, st.ent.constness);
                        auto rv = findSourceTrait(sp, b.mPath, st.ent.path->mBindings.type.binding, name, ns, cb);
                        if (rv != HIRGenericPath()) {
                            return ms.monomorphGenericpath(sp, rv, /*allow_infer=*/true);
                        }
                    }
                    return HIRGenericPath();
                } else {
                    BUG(sp, "Unbound path");
                }
            } else {
                BUG(sp, "Not a trait: " << path << " : " << pb.tagStr());
            }
        }

        std::pair<RcString, HIRPathParams> getAtyNode(const Span& sp, const ASTPathNode& pn) {
            auto args = mCtx.LowerHIRPathParams(sp, pn.args(), false);
            if (args.hasParams()) {
                TODO(sp, "Handle ATYs with args");
            }
            return std::make_pair(pn.name(), std::move(args));
        }
    };

    for (const auto& e : path.nodes().back().args().entries) {
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
                if (assoc.first.args().isRtn) {
                    ERROR(sp, E0000, "Return-type notation does not support equality constraints");
                }
                auto nameArgs = H(*this).getAtyNode(sp, assoc.first);
                auto srcTrait = H(*this).findSourceTrait(sp, rv.mPath, path.mBindings.type.binding, nameArgs.first, H::Namespace::Type, MonomorphiserNop(mCrate->types));
                DEBUG("src_trait = " << srcTrait << " for " << assoc.first);
                rv.typeBounds.insert(::std::make_pair(nameArgs.first, HIRTraitPath::AtyEqual{std::move(srcTrait), std::move(nameArgs.second), LowerHIRType(assoc.second)}));
            }
            TU_ARMA(AssociatedTyBound, assoc) {
                if (!ignoreBounds) {
                    ERROR(sp, E0000, "Associated type trait bounds not allowed here - " << path);
                } else {
                    auto nameArgs = H(*this).getAtyNode(sp, assoc.first);
                    const auto sourceName = nameArgs.first;
                    const auto ns = assoc.first.args().isRtn ? H::Namespace::Function : H::Namespace::Type;
                    auto srcTrait = H(*this).findSourceTrait(sp, rv.mPath, path.mBindings.type.binding, sourceName, ns, MonomorphiserNop(mCrate->types));
                    if (assoc.first.args().isRtn) {
                        nameArgs.first = RcString::newInterned(FMT(ATY_PREFIX_ERASED << sourceName << "_0"));
                    }
                    DEBUG("src_trait = " << srcTrait << " for " << assoc.first);
                    //if(src_trait == ::HIR::GenericPath())
                    auto it = rv.traitBounds.insert(std::make_pair(nameArgs.first, HIRTraitPath::AtyBound{std::move(srcTrait), std::move(nameArgs.second), {}}));
                    for (const auto& trait : assoc.second) {
                        it.first->second.traits.push_back(LowerHIRTraitPath(sp, trait, {}, /*ignore_bounds*/ false));
                    }
                }
            }
        }
    }

    return rv;
}

HIRPath AST2HIR::LowerHIRPath(const Span& sp, const ASTPath& path, FromASTPathClass pc) {
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
            return HIRPath(LowerHIRGenericPath(sp, path, pc));
        }
        TU_ARMA(UFCS, e) {
            if (e.nodes.size() == 0) {
                if (!(!e.trait || e.trait->isValid())) {
                    TODO(sp, "Handle UFCS w/ trait and no nodes - " << path);
                }
                auto type = LowerHIRType(e.type);
                ASSERT_BUG(sp, type->is_Path(), "No nodes and non-Path type - " << path);
                return type->as_Path().path.clone();
            }
            if (e.nodes.size() > 1) {
                TODO(sp, "Handle UFCS with multiple nodes - " << path);
            }
            // - No associated type bounds allowed in UFCS paths
            auto params = LowerHIRPathParams(sp, e.nodes.front().args(), /*allow_assoc*/ false);
            auto itemName = e.nodes[0].name();
            if (e.nodes.front().args().isRtn) {
                itemName = RcString::newInterned(FMT(ATY_PREFIX_ERASED << itemName << "_0"));
            }

            if (!e.trait || !e.trait->isValid()) {
                return HIRPath(HIRPath::Data::make_UfcsUnknown({LowerHIRType(e.type), mv$(itemName), mv$(params)}));
            } else {
                return HIRPath(HIRPath::Data::make_UfcsKnown({LowerHIRType(e.type), LowerHIRGenericPath(sp, *e.trait, FromASTPathClass::Type), mv$(itemName), mv$(params)}));
            }
        }
    }
    throw "BUGCHECK: Reached end of LowerHIR_Path";
}

namespace {
    class TraitObjectLowering {
        AST2HIR& mCtx;
        const Span& mSpan;
        HIRTypeData::Data_TraitObject& out;
        ::std::unordered_set<const void*> activeAliases;

        bool hasPrincipal() const {
            return !out.mTrait.mPath.mPath.components().empty();
        }

        void addTrait(HIRTraitPath trait, bool isMarker) {
            if (isMarker) {
                if (!trait.typeBounds.empty() || !trait.traitBounds.empty()) {
                    ERROR(mSpan, E0000, "Associated type bounds on auto trait " << trait.mPath);
                }
                out.markers.push_back(mv$(trait.mPath));
                return;
            }

            if (hasPrincipal()) {
                ERROR(mSpan, E0000, "Multiple data traits in trait object: " << out.mTrait.mPath << " and " << trait.mPath);
            }
            out.mTrait = mv$(trait);
        }

        void applyAliasBounds(HIRTraitPath& aliasPath, bool hadPrincipal) {
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

        ActiveAlias enterAlias(const void* key, const HIRGenericPath& path) {
            if (!activeAliases.insert(key).second) {
                ERROR(mSpan, E0000, "Recursive trait alias in trait object: " << path);
            }
            return ActiveAlias{activeAliases, key};
        }

        void addAstPath(HIRTraitPath path, const ASTPathBindingType& binding) {
            if (const auto* trait = binding.opt_Trait()) {
                ASSERT_BUG(mSpan, trait->trait_ || trait->hir, "Null trait binding for " << path.mPath);
                addTrait(mv$(path), trait->trait_ ? trait->trait_->isMarker() : trait->hir->mIsMarker);
            } else if (const auto* alias = binding.opt_TraitAlias()) {
                expandAstAlias(mv$(path), *alias);
            } else {
                BUG(mSpan, "Not a trait or trait alias: " << path.mPath << " (" << binding.tagStr() << ")");
            }
        }

        void addHirPath(HIRTraitPath path) {
            const auto& item = mCtx.mCrate->getTypeitemByPath(mSpan, path.mPath.mPath);
            if (const auto* trait = item.opt_Trait()) {
                addTrait(mv$(path), trait->mIsMarker);
            } else if (const auto* alias = item.opt_TraitAlias()) {
                expandHirAlias(mv$(path), *alias);
            } else {
                BUG(mSpan, "Trait alias expanded to non-trait path " << path.mPath << " (" << item.tagStr() << ")");
            }
        }

        void expandAstAlias(HIRTraitPath aliasPath, const ASTPathBindingType::Data_TraitAlias& binding) {
            const void* key = binding.trait_ ? static_cast<const void*>(binding.trait_) : static_cast<const void*>(binding.hir);
            ASSERT_BUG(mSpan, key, "Null trait alias binding for " << aliasPath.mPath);
            auto active = enterAlias(key, aliasPath.mPath);
            const bool hadPrincipal = hasPrincipal();

            if (binding.trait_) {
                bool traitRequiresSized = false;
                auto paramsDef = mCtx.LowerHIRGenericParams(binding.trait_->params, &traitRequiresSized);
                auto params = ConvertHIRCompleteAliasParams(mCtx.mCrate->types, mSpan, paramsDef, aliasPath.mPath, false);
                auto monomorph = MonomorphStatePtr(mCtx.mCrate->types, nullptr, &params, nullptr);
                for (const auto& bound : binding.trait_->traits) {
                    auto trait = mCtx.LowerHIRTraitPath(bound.sp, *bound.ent.path, bound.ent.hrbs, false, bound.ent.constness);
                    addAstPath(monomorph.monomorphTraitpath(mSpan, trait, false), bound.ent.path->mBindings.type.binding);
                }
            } else {
                ASSERT_BUG(mSpan, binding.hir, "Null trait alias binding for " << aliasPath.mPath);
                expandHirAliasContents(aliasPath, *binding.hir);
            }

            applyAliasBounds(aliasPath, hadPrincipal);
        }

        void expandHirAliasContents(const HIRTraitPath& aliasPath, const HIRTraitAlias& alias) {
            auto params = ConvertHIRCompleteAliasParams(mCtx.mCrate->types, mSpan, alias.mParams, aliasPath.mPath, false);
            auto monomorph = MonomorphStatePtr(mCtx.mCrate->types, nullptr, &params, nullptr);
            for (const auto& bound : alias.traits) {
                auto trait = bound.clone();
                addHirPath(monomorph.monomorphTraitpath(mSpan, trait, false));
            }
        }

        void expandHirAlias(HIRTraitPath aliasPath, const HIRTraitAlias& alias) {
            auto active = enterAlias(&alias, aliasPath.mPath);
            const bool hadPrincipal = hasPrincipal();
            expandHirAliasContents(aliasPath, alias);
            applyAliasBounds(aliasPath, hadPrincipal);
        }

    public:
        TraitObjectLowering(AST2HIR& ctx, const Span& span, HIRTypeData::Data_TraitObject& out)
            : mCtx(ctx)
            , mSpan(span)
            , out(out)
        {
        }

        void add(const ::TypeTraitPath& bound) {
            auto path = mCtx.LowerHIRTraitPath(mSpan, *bound.path, bound.hrbs, false, bound.constness);
            addAstPath(mv$(path), bound.path->mBindings.type.binding);
        }
    };
}

HIRTypeRef AST2HIR::LowerHIRType(::ASTType* ty) {
    TU_MATCH_HDRA( (ty->mData), {)
    TU_ARMA(None, e) {
            BUG(ty->span(), "TypeData::None");
        }
        TU_ARMA(Bang, e) {
            return mCrate->types.diverge();
        }
        TU_ARMA(Any, e) {
            return mCrate->types.infer();
        }
        TU_ARMA(Unit, e) {
            return mCrate->types.unit();
        }
        TU_ARMA(Macro, e) {
            BUG(ty->span(), "TypeData::Macro");
        }
        TU_ARMA(Primitive, e) {
            switch (e.coreType) {
                case CORETYPE_BOOL:
                    return mCrate->types.primitive(HIRCoreType::Bool);
                case CORETYPE_CHAR:
                    return mCrate->types.primitive(HIRCoreType::Char);
                case CORETYPE_STR:
                    return mCrate->types.primitive(HIRCoreType::Str);
                case CORETYPE_F16:
                    return mCrate->types.primitive(HIRCoreType::F16);
                case CORETYPE_F32:
                    return mCrate->types.primitive(HIRCoreType::F32);
                case CORETYPE_F64:
                    return mCrate->types.primitive(HIRCoreType::F64);
                case CORETYPE_F128:
                    return mCrate->types.primitive(HIRCoreType::F128);

                case CORETYPE_I8:
                    return mCrate->types.primitive(HIRCoreType::I8);
                case CORETYPE_U8:
                    return mCrate->types.primitive(HIRCoreType::U8);
                case CORETYPE_I16:
                    return mCrate->types.primitive(HIRCoreType::I16);
                case CORETYPE_U16:
                    return mCrate->types.primitive(HIRCoreType::U16);
                case CORETYPE_I32:
                    return mCrate->types.primitive(HIRCoreType::I32);
                case CORETYPE_U32:
                    return mCrate->types.primitive(HIRCoreType::U32);
                case CORETYPE_I64:
                    return mCrate->types.primitive(HIRCoreType::I64);
                case CORETYPE_U64:
                    return mCrate->types.primitive(HIRCoreType::U64);

                case CORETYPE_I128:
                    return mCrate->types.primitive(HIRCoreType::I128);
                case CORETYPE_U128:
                    return mCrate->types.primitive(HIRCoreType::U128);

                case CORETYPE_INT:
                    return mCrate->types.primitive(HIRCoreType::Isize);
                case CORETYPE_UINT:
                    return mCrate->types.primitive(HIRCoreType::Usize);
                case CORETYPE_ANY:
                    TODO(ty->span(), "TypeData::Primitive - CORETYPE_ANY");
                case CORETYPE_INVAL:
                    BUG(ty->span(), "TypeData::Primitive - CORETYPE_INVAL");
            }
        }
        TU_ARMA(Tuple, e) {
            HIRTypeData::Data_Tuple v;
            for (const auto& st : e.innerTypes) {
                v.push_back(LowerHIRType(st));
            }
            return mCrate->types.tuple(mv$(v));
        }
        TU_ARMA(Borrow, e) {
            auto cl = (e.isMut ? HIRBorrowType::Unique : HIRBorrowType::Shared);
            return mCrate->types.borrow(cl, LowerHIRType(e.inner));
        }
        TU_ARMA(Pointer, e) {
            auto cl = (e.isMut ? HIRBorrowType::Unique : HIRBorrowType::Shared);
            return mCrate->types.pointer(cl, LowerHIRType(e.inner));
        }
        TU_ARMA(Array, e) {
            auto inner = LowerHIRType(e.inner);
            if (e.size) {
                // If the size expression is an unannotated or usize integer literal, don't bother converting the expression
                if (const auto* ptr = cast<const ASTExprNodeInteger>(&*e.size)) {
                    if (ptr->datatype == CORETYPE_UINT || ptr->datatype == CORETYPE_ANY) {
                        // TODO: Chage the HIR format to support very large arrays
                        if (ptr->mValue >= U128(UINT64_MAX)) {
                            ERROR(ty->span(), E0000, "Array size out of bounds - 0x" << ::std::hex << ptr->mValue << " > 0x" << UINT64_MAX << " in " << ::std::dec << ty);
                        }
                        return mCrate->types.array(inner, ptr->mValue.truncateU64());
                    }
                }
                if (const auto* ptr = cast<const ASTExprNodeNamedValue>(&*e.size)) {
                    if (ptr->mPath.isTrivial()) {
                        auto gr = HIRGenericRef(ptr->mPath.asTrivial(), ptr->mPath.mBindings.value.binding.as_Generic().index);
                        return mCrate->types.array(inner, HIRConstGeneric(mv$(gr)));
                    }
                }

                return mCrate->types.array(inner, HIRConstGeneric::make_Unevaluated(std::make_unique<HIRConstGenericUnevaluated>(LowerHIRExpr(e.size))));
            } else {
                return mCrate->types.array(inner, HIRConstGeneric::make_Infer({}));
            }
        }
        TU_ARMA(Slice, e) {
            auto inner = LowerHIRType(e.inner);
            return mCrate->types.slice(inner);
        }
        TU_ARMA(Path, e) {
            if (const auto* l = e->cls.opt_Local()) {
                unsigned int slot;
                // NOTE: TypeParameter is unused
                if (const auto* p = e->mBindings.type.binding.opt_TypeParameter()) {
                    slot = p->slot;
                } else {
                    BUG(ty->span(), "Unbound local encountered in " << *e);
                }
                return mCrate->types.generic(l->name, slot);
            } else if (e->mBindings.type.path.crate == CRATE_BUILTINS) {
                return LowerHIRType(mkType(*ty->pool, ty->span(), coretypeFromstring(e->mBindings.type.path.nodes.back().c_str())));
            } else {
                return mCrate->types.path(LowerHIRPath(ty->span(), *e, FromASTPathClass::Type), {});
            }
        }
        TU_ARMA(TraitObject, e) {
            HIRTypeData::Data_TraitObject v;
            TraitObjectLowering lowering(*this, ty->span(), v);
            for (const auto& t : e.traits) {
                DEBUG("t = " << *t.path);
                lowering.add(t);
            }
            // Sort markers so downstream can compare properly
            ::std::sort(v.markers.begin(), v.markers.end());
            v.markers.erase(::std::unique(v.markers.begin(), v.markers.end()), v.markers.end());
            return mCrate->types.intern(HIRTypeData::make_TraitObject(mv$(v)));
        }
        TU_ARMA(ErasedType, e) {
            ASSERT_BUG(ty->span(), e->traits.size() > 0, "ErasedType with no traits");

            // TODO: There can be associated type bounds, those need to be propagated

            ::std::vector<HIRTraitPath> traits;
            for (const auto& t : e->traits) {
                DEBUG("t = " << *t.path);
                // TODO: Handle ATY bounds
                traits.push_back(LowerHIRTraitPath(ty->span(), *t.path, t.hrbs, /*allow_aty_trait_bounds=*/true, t.constness));
            }
            bool isSized = true;
            for (const auto& t : e->maybeTraits) {
                auto tp = LowerHIRTraitPath(ty->span(), *t.path, t.hrbs, /*allow_aty_trait_bounds=*/true);
                if (tp.mPath.mPath == pathSized) {
                    isSized = false;
                } else {
                    TODO(ty->span(), "Optional trait (not Sized) - " << ty);
                }
            }
            TypeDataErasedTypeInner inner;
            if (mImplTraitSource.path) {
                if (mImplTraitSource.paramsInner && mImplTraitSource.paramsInner->isGeneric()) {
                    TODO(ty->span(), "Handle multi-layered generic erased type (used in a GAT)");
                }
                inner = TypeDataErasedTypeInner(TypeDataErasedTypeInner::Data_Alias{mImplTraitSource.paramsOuter->makeNopParams(mCrate->types, 0), std::make_shared<HIRTypeDataErasedTypeAliasInner>(*mImplTraitSource.path, *mImplTraitSource.paramsOuter)});
            } else {
                inner = TypeDataErasedTypeInner::Data_Fcn{HIRPath(HIRSimplePath()), 0}; // Populated in bind, could be populated now?
            }
            return mCrate->types.intern(HIRTypeData::make_ErasedType({isSized, mv$(traits), mv$(inner), e->use ? LowerHIRPathParams(ty->span(), *e->use, false) : HIRPathParams(), e->use ? HIRTypeDataErasedType::Use::Present : (e->isEdition2024OrLater ? HIRTypeDataErasedType::Use::Omitted2024 : HIRTypeDataErasedType::Use::OmittedOld)}));
        }
        TU_ARMA(Function, e) {
            ::std::vector<HIRTypeRef> args;
            for (const auto& arg : e.info.argTypes) {
                args.push_back(LowerHIRType(arg));
            }
            HIRTypeDataFunctionPointer f{e.info.isUnsafe, e.info.isVariadic, RcString::newInterned(e.info.mAbi), LowerHIRType(e.info.mRettype), mv$(args)};
            if (f.mAbi == "") {
                f.mAbi = RcString::newInterned(ABI_RUST);
            }
            return mCrate->types.function(mv$(f));
        }
        TU_ARMA(Generic, e) {
            assert(e.index < 0x10000);
            return mCrate->types.generic(e.name, e.index);
        }
    }
    throw "BUGCHECK: Reached end of LowerHIR_Type";
}

HIRTypeAlias AST2HIR::LowerHIRTypeAlias(const HIRItemPath& p, const ASTTypeAlias& ta) {
    assert(!mImplTraitSource.path);
    auto params = LowerHIRGenericParams(ta.params(), nullptr);
    mImplTraitSource = ImplTraitSource(&p, &params);
    auto ty = LowerHIRType(ta.type());
    //}
    mImplTraitSource = ImplTraitSource();
    return HIRTypeAlias{std::move(params), ::std::move(ty)};
}

namespace {
    template <typename T>
    HIRVisEnt<T> newVisent(HIRPublicity pub, T v) {
        return HIRVisEnt<T>{pub, mv$(v)};
    }

    HIRSimplePath getParentModule(const HIRItemPath& p) {
        const HIRItemPath* parentIp = p.parent;
        assert(parentIp);
        while (parentIp->name && parentIp->name[0] == '#') {
            parentIp = parentIp->parent;
            assert(parentIp);
        }
        return parentIp->getSimplePath();
    }
}

tStructFields AST2HIR::LowerHIRStructFields(HIRItemPath path, const HIRGenericParams& params, const ::std::vector<ASTStructItem>& inFields, HIRModule& outMod) {
    HIRStruct::Data::Data_Named fields;
    for (const auto& field : inFields) {
        auto type = LowerHIRType(field.mType);
        ::std::unique_ptr<HIRGenericPath> fieldDefault;
        if (field.defaultValue) {
            // NOTE: I'd love to have this be a `Constant`, but that would require duplicating the type and the params
            // meh. Lazy option is to just duplicate
            auto name = RcString::newInterned(FMT(path.getName() << "#default_" << field.mName));
            outMod.valueItems.insert(std::make_pair(name, mCrate->pool->make<HIRVisEnt<HIRValueItem>>(HIRVisEnt<HIRValueItem>{HIRPublicity::newGlobal(), HIRValueItem(HIRConstant(params.clone(), type, LowerHIRExpr(field.defaultValue)))})));
            fieldDefault = std::make_unique<HIRGenericPath>((*path.parent + name).getSimplePath(), params.makeNopParams(mCrate->types, 0));
        }
        fields.push_back(HIRStructField{field.mName, LowerHIRVis(getParentModule(path), field.vis), std::move(type), std::move(fieldDefault)});
    }
    return fields;
}

HIRStruct AST2HIR::LowerHIRStruct(const Span& sp, HIRItemPath path, const ASTStruct& ent, const ASTAttributeList& attrs, HIRModule& outMod) {
    TRACE_FUNCTION_F(path);
    HIRStruct::Data data;

    auto modPath = getParentModule(path);
    auto getVis = [&](const ASTVisibility& vis) {
        return LowerHIRVis(modPath, vis);
    };

    auto rv = HIRStruct{LowerHIRGenericParams(ent.params(), nullptr), HIRStruct::Repr::Rust, {}};

    TU_MATCH_HDRA( (ent.mData), {)
    TU_ARMA(Unit, e) {
            rv.mData = HIRStruct::Data::make_Unit({});
        }
        TU_ARMA(Tuple, e) {
            HIRStruct::Data::Data_Tuple fields;

            for (const auto& field : e.ents) {
                fields.push_back({getVis(field.vis), LowerHIRType(field.mType)});
            }

            rv.mData = HIRStruct::Data::make_Tuple(mv$(fields));
        }
        TU_ARMA(Struct, e) {
            auto fields = LowerHIRStructFields(path, rv.mParams, e.ents, outMod);
            rv.mData = HIRStruct::Data::make_Named(mv$(fields));
        }
    }

    // Determine the repr
    {
        switch (ent.markings.repr) {
            case ASTStruct::Markings::Repr::Rust:
                rv.repr = HIRStruct::Repr::Rust;
                break;
            case ASTStruct::Markings::Repr::C:
                rv.repr = HIRStruct::Repr::C;
                break;
            case ASTStruct::Markings::Repr::Simd:
                rv.repr = HIRStruct::Repr::Simd;
                break;
            case ASTStruct::Markings::Repr::Transparent:
                rv.repr = HIRStruct::Repr::Transparent;
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
        // In 1.90 this no longer marks wrappers as nonzero; scalar limits carry
        // the layout information instead.
    }
    rv.structMarkings.isFundamental = attrs.has("fundamental");
    const auto& simplePath = path.getSimplePath();
    rv.structMarkings.isNoNiche = simplePath == mCrate->getLangItemPathOpt("unsafe_cell") || simplePath == mCrate->getLangItemPathOpt("unsafe_pinned");
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
        const HIRTypeData* ty = nullptr;
        const HIRTypeData* ty2 = nullptr;
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
            HIRCoreType ct = HIRCoreType::Str;
            if (ty->is_Primitive()) {
                ct = ty->as_Primitive();
            }
            switch (ct) {
                case HIRCoreType::U8:
                    max = U128(0xFF);
                    break;
                case HIRCoreType::U16:
                    max = U128(UINT16_MAX);
                    break;
                case HIRCoreType::U32:
                    max = U128(UINT32_MAX);
                    break;
                case HIRCoreType::U64:
                    max = U128(UINT64_MAX);
                    break;
                case HIRCoreType::U128:
                    break;
                case HIRCoreType::Usize:
                    max = U128(TGT_PTR_MAX);
                    break;

                case HIRCoreType::I8:    //max = 0x7F;     break;
                case HIRCoreType::I16:   //max = INT16_MAX;   break;
                case HIRCoreType::I32:   //max = INT32_MAX; break;
                case HIRCoreType::I64:   //max = INT64_MAX;   break;
                case HIRCoreType::I128:  //ignore = true;  break;
                case HIRCoreType::Isize: //max = TGT_PTR_MAX/2+1;   break;
                    // Downstream treats this as unsigned
                    ignore = true;
                    break;

                default:
                    ignore = true;
                    break;
            }
        }

        if (!ignore) {
            if (ent.markings.scalarValidStartSet) {
                if (ent.markings.scalarValidStart < min) {
                }
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

HIREnum AST2HIR::LowerHIREnum(HIRItemPath path, const ASTEnum& ent, const ASTAttributeList& attrs, ::std::function<void(RcString, HIRStruct)> pushStruct, HIRModule& outMod) {
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
    auto repr = HIREnum::Repr::Auto;
    switch (ent.markings.repr) {
        case ASTEnum::Markings::Repr::Rust:
            repr = HIREnum::Repr::Auto;
            break;
        case ASTEnum::Markings::Repr::U8:
            repr = HIREnum::Repr::U8;
            break;
        case ASTEnum::Markings::Repr::U16:
            repr = HIREnum::Repr::U16;
            break;
        case ASTEnum::Markings::Repr::U32:
            repr = HIREnum::Repr::U32;
            break;
        case ASTEnum::Markings::Repr::U64:
            repr = HIREnum::Repr::U64;
            break;
        case ASTEnum::Markings::Repr::U128:
            repr = HIREnum::Repr::U128;
            break;
        case ASTEnum::Markings::Repr::Usize:
            repr = HIREnum::Repr::Usize;
            break;
        case ASTEnum::Markings::Repr::I8:
            repr = HIREnum::Repr::I8;
            break;
        case ASTEnum::Markings::Repr::I16:
            repr = HIREnum::Repr::I16;
            break;
        case ASTEnum::Markings::Repr::I32:
            repr = HIREnum::Repr::I32;
            break;
        case ASTEnum::Markings::Repr::I64:
            repr = HIREnum::Repr::I64;
            break;
        case ASTEnum::Markings::Repr::I128:
            repr = HIREnum::Repr::I128;
            break;
        case ASTEnum::Markings::Repr::Isize:
            repr = HIREnum::Repr::Isize;
            break;
    }

    auto params = LowerHIRGenericParams(ent.params(), nullptr);

    HIREnum::Class data;
    if (ent.variants().size() > 0 && !hasData) {
        ::std::vector<HIREnum::ValueVariant> variants;
        for (const auto& var : ent.variants()) {
            // TODO: Quick consteval on the expression?
            variants.push_back({var.mName, LowerHIRExpr(var.discriminantValue), U128(0)});
        }

        data = HIREnum::Class::make_Value({mv$(variants)});
    }
    // NOTE: empty enums are encoded as empty Data enums
    else {
        ::std::vector<HIREnum::DataVariant> variants;
        const auto variantRepr = isReprC || repr != HIREnum::Repr::Auto ? HIRStruct::Repr::C : HIRStruct::Repr::Rust;
        for (const auto& var : ent.variants()) {
            if (var.mData.is_Unit() && ent.markings.alignValue == 0) {
                // TODO: Should this make its own unit-like struct?
                variants.push_back({var.mName, false, mCrate->types.unit()});
            } else {
                HIRStruct::Data data;
                if (var.mData.is_Unit()) {
                    data = HIRStruct::Data::make_Unit({});
                } else if (const auto* ve = var.mData.opt_Tuple()) {
                    HIRStruct::Data::Data_Tuple fields;
                    for (const auto& field : ve->mItems) {
                        fields.push_back(newVisent(HIRPublicity::newGlobal(), LowerHIRType(field.mType)));
                    }
                    data = HIRStruct::Data::make_Tuple(mv$(fields));
                } else if (const auto* ve = var.mData.opt_Struct()) {
                    auto fields = LowerHIRStructFields(path, params, ve->fields, outMod);
                    data = HIRStruct::Data::make_Named(mv$(fields));
                } else {
                    throw "";
                }

                auto tyName = RcString::newInterned(FMT(path.name << "#" << var.mName));
                auto variantStruct = HIRStruct{LowerHIRGenericParams(ent.params(), nullptr), variantRepr, mv$(data)};
                variantStruct.forcedAlignment = ent.markings.alignValue;
                pushStruct(tyName, mv$(variantStruct));
                auto tyIpath = path;
                tyIpath.name = tyName.c_str();
                auto tyPath = tyIpath.getFullPath();
                // Add type params
                tyPath.mData.as_Generic().mParams = params.makeNopParams(mCrate->types, 0);
                variants.push_back({var.mName, var.mData.is_Struct(), mCrate->types.path(mv$(tyPath), {})});
            }

            if (var.discriminantValue) {
                if (repr == HIREnum::Repr::Auto) {
                    ERROR(var.discriminantValue.node().span(), E0000, "Discrimiant value set on enum with no `repr` set");
                }
                variants.back().discriminantExpr = LowerHIRExpr(var.discriminantValue);
            }
        }

        switch (repr) {
            case HIREnum::Repr::Auto:
                break;
            default:
                // NOTE:
                // - librustc_llvm has `#[repr(C)] enum AttributePlace { Argument(u32), Function }`
                // - `rustc-1.19.0-src\src\vendor\idna\src\uts46.rs:33` has `#[repr(u16)]`

                // NOTE: We save the repr for use in `trans/target.cpp`
                // https://github.com/rust-lang/rfcs/blob/master/text/2195-really-tagged-unions.md
                // - `repr(int)` packs the tag into the variants (which can be more efficient for alignment, with `Variant(u8, u16)`)
                // - `repr(C,int)` has the tag before variants (so will be less alignment efficient)
                break;
        }

        data = HIREnum::Class::make_Data(mv$(variants));
    }

    return HIREnum{mv$(params), isReprC, repr, mv$(data)};
}

HIRUnion AST2HIR::LowerHIRUnion(HIRItemPath path, const ASTUnion& f, const ASTAttributeList& attrs) {
    auto modPath = getParentModule(path);
    auto getVis = [&](const ASTVisibility& vis) {
        return LowerHIRVis(modPath, vis);
    };

    auto repr = HIRUnion::Repr::Rust;
    switch (f.markings.repr) {
        case ASTUnion::Markings::Repr::Rust:
            repr = HIRUnion::Repr::Rust;
            break;
        case ASTUnion::Markings::Repr::C:
            repr = HIRUnion::Repr::C;
            break;
        case ASTUnion::Markings::Repr::Transparent:
            repr = HIRUnion::Repr::Transparent;
            break;
    }

    HIRStruct::Data::Data_Named variants;
    for (const auto& field : f.mVariants) {
        variants.push_back(HIRStructField{field.mName, getVis(field.vis), LowerHIRType(field.mType), {}});
    }

    return HIRUnion{LowerHIRGenericParams(f.mParams, nullptr), repr, mv$(variants)};
}

namespace {
    class RpititTypeCollector: public HIRVisitor {
        ::std::function<void(unsigned, HIRTypeRef)> callback;
        unsigned index = 0;

    public:
        RpititTypeCollector(HIRTypeInterner& types, ::std::function<void(unsigned, HIRTypeRef)> callback)
            : HIRVisitor(nullptr, types)
            , callback(std::move(callback))
        {
        }

        void visitType(HIRTypeRef& ty) override {
            HIRVisitor::visitType(ty);
            const auto* erased = ty->opt_ErasedType();
            if (erased && erased->inner.is_Fcn()) {
                callback(index++, ty);
            }
        }
    };
}

HIRTrait AST2HIR::LowerHIRTrait(HIRSimplePath traitPath, const ASTTrait& f, const ASTAttributeList& attrs) {
    TRACE_FUNCTION_F(traitPath);
    traitPath.updateCrateName(mCrateName);

    bool traitReqiresSized = false;
    auto params = LowerHIRGenericParams(f.params(), &traitReqiresSized);

    ::std::vector<HIRTraitPath> supertraits;
    for (const auto& st : f.supertraits()) {
        supertraits.push_back(LowerHIRTraitPath(st.sp, *st.ent.path, st.ent.hrbs, true, st.ent.constness));
        DEBUG("Supertrait " << supertraits.back());
    }
    HIRTrait rv{mv$(params), mv$(supertraits)};
    rv.isConst = attrs.has("const_trait");
    if (const auto* attr = attrs.get("rustc_skip_during_method_dispatch")) {
        TTStream tokens(attr->span(), ParseState(), attr->data());
        tokens.getTokenCheck(TOK_PAREN_OPEN);
        while (tokens.lookahead(0) != TOK_PAREN_CLOSE) {
            const auto name = tokens.getTokenCheck(TOK_IDENT).ident().name;
            if (name == "array") {
                rv.skipArrayDuringMethodDispatch = true;
            } else if (name == "boxed_slice") {
                rv.skipBoxedSliceDuringMethodDispatch = true;
            } else {
                ERROR(attr->span(), E0000, "Unknown rustc_skip_during_method_dispatch receiver `" << name << "`");
            }
            if (!tokens.getTokenIf(TOK_COMMA)) {
                break;
            }
        }
        tokens.getTokenCheck(TOK_PAREN_CLOSE);
    }

    // HACK: Add a bound of Self: ThisTrait for parts of typeck (TODO: Remove this, it's evil)
    {
        auto thisTrait = HIRGenericPath(traitPath);
        thisTrait.mParams = rv.mParams.makeNopParams(mCrate->types, 0);
        rv.mParams.bounds.push_back(HIRGenericBound::make_TraitBound({mCrate->types.self(), HIRTraitPath(mv$(thisTrait))}));
    }

    for (const auto& item : f.items()) {
        auto traitIp = HIRItemPath(traitPath);
        auto itemPath = HIRItemPath(traitIp, item.name.c_str());

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
                ::std::vector<HIRTraitPath> traitBounds;
                auto gps = LowerHIRGenericParams(i.params(), &isSized);

                auto selfBounds = LowerHIRGenericParams(i.selfBounds, &isSized);
                for (auto& b : selfBounds.bounds) {
                TU_MATCH_HDRA( (b), {)
                TU_ARMA(TraitBound, be) {
                            ASSERT_BUG(item.span, be.type->as_Generic().binding == GENERICSelf, be.type);
                            traitBounds.push_back(mv$(be.trait));
                        }
                        TU_ARMA(TypeEquality, be) {
                            BUG(item.span, "Unexpected type equality bound on associated type");
                        }
                }
                }
                rv.types.insert(::std::make_pair(item.name, HIRAssociatedType{mv$(gps), isSized, mv$(traitBounds), LowerHIRType(i.type())}));
            }
            TU_ARMA(Function, i) {
                auto fcn = LowerHIRFunction(itemPath, item.attrs, i, mCrate->types.self());
                RpititTypeCollector(mCrate->types, [&](unsigned index, HIRTypeRef type) {
                    const auto& erased = type->as_ErasedType();
                    auto name = RcString::newInterned(FMT(ATY_PREFIX_ERASED << item.name << "_" << index));
                    ::std::vector<HIRTraitPath> bounds;
                    for (const auto& bound : erased.traits) {
                        bounds.push_back(bound.clone());
                    }
                    auto inserted = rv.types.insert(std::make_pair(name, HIRAssociatedType{fcn.mParams.clone(), erased.isSized, std::move(bounds), mCrate->types.infer()}));
                    ASSERT_BUG(item.span, inserted.second, "Synthetic RPITIT associated type collides with " << name);
                }).visitType(fcn.returnType);
                if (rv.isConst) {
                    fcn.isConst = true;
                }
                fcn.saveCode = true;
                rv.values.insert(::std::make_pair(item.name, HIRTraitValueItem::make_Function(mv$(fcn))));
            }
            TU_ARMA(Static, i) {
                if (i.sClass() == ASTStatic::CONST) {
                    rv.values.insert(::std::make_pair(item.name, HIRTraitValueItem::make_Constant(HIRConstant(HIRGenericParams{}, LowerHIRType(i.type()), LowerHIRExpr(i.value())))));
                } else {
                    HIRLinkage linkage;
                    rv.values.insert(::std::make_pair(item.name, HIRTraitValueItem::make_Static(HIRStatic(mv$(linkage), (i.sClass() == ASTStatic::MUT), LowerHIRType(i.type()), LowerHIRExpr(i.value())))));
                }
            }
        }
    }

    rv.mIsMarker = f.isMarker();
    rv.isCoinductive = rv.mIsMarker || attrs.has("rustc_coinductive");
    rv.isFundamental = attrs.has("fundamental");

    return rv;
}

HIRTraitAlias AST2HIR::LowerHIRTraitAlias(const Span& sp, HIRItemPath p, const ASTTraitAlias& f) {
    bool traitReqiresSized = false;

    HIRTraitAlias ta;
    ta.mParams = LowerHIRGenericParams(f.params, &traitReqiresSized);
    for (const auto& t : f.traits) {
        ta.traits.push_back(LowerHIRTraitPath(t.sp, *t.ent.path, t.ent.hrbs, false, t.ent.constness));
    }

    return ta;
}

HIRFunction AST2HIR::LowerHIRFunction(HIRItemPath p, const ASTAttributeList& attrs, const ASTFunction& f, const HIRTypeData* realSelfType) {
    static Span sp;

    TRACE_FUNCTION_F(p);

    if (const auto* attr = attrs.get("define_opaque")) {
        TTStream tokens(attr->span(), ParseState(), attr->data());
        tokens.getTokenCheck(TOK_PAREN_OPEN);
        while (tokens.lookahead(0) != TOK_PAREN_CLOSE) {
            auto opaquePath = ParsePath(tokens, PATH_GENERIC_NONE);
            ASSERT_BUG(attr->span(), !opaquePath.nodes().empty(), "Empty path in #[define_opaque]");
            mCrate->opaqueTypeDefiners[opaquePath.nodes().back().name()].push_back(p.getFullPath());
            if (!tokens.getTokenIf(TOK_COMMA)) {
                break;
            }
        }
        tokens.getTokenCheck(TOK_PAREN_CLOSE);
    }

    ::std::vector<::std::pair<HIRPattern, HIRTypeRef>> args;
    for (const auto& arg : f.args()) {
        args.push_back(::std::make_pair(LowerHIRPattern(arg.pat), LowerHIRType(arg.ty)));
    }

    auto receiver = HIRFunction::Receiver::Free;

    if (args.size() > 0 && args.front().first.mBindings.size() > 0 && args.front().first.mBindings[0].mName == "self") {
        const auto& sp = f.args()[0].pat.span();
        auto& argSelfTy = args.front().second;

        struct Ivcr {
            AST2HIR& mCtx;
            const Span& sp;
            const HIRTypeData* realSelfType;

            Ivcr(AST2HIR& ctx, const Span& sp, const HIRTypeData* realSelfType)
                : mCtx(ctx)
                , sp(sp)
                , realSelfType(realSelfType)
            {
            }

            bool isValidCustomReceiver(HIRTypeRef& ty) const {
                // - The path must include Self as a (the only?) type param.
                if (ty == mCtx.mCrate->types.self()) {
                    return true;
                } else if (ty == realSelfType) {
                    ty = mCtx.mCrate->types.self();
                    return true;
                } else if (ty->is_Path()) {
                    auto data = ty->cloneData();
                    auto& e = data.as_Path();
                    if (auto* pe = e.path.mData.opt_Generic()) {
                        if (pe->mParams.types.size() == 0) {
                            ERROR(sp, E0000, "Receiver type should have one type param - " << ty);
                        }
                        //   TODO(sp, "Receiver types with more than one param - " << arg_self_ty);
                        //}

                        // TODO: Allow if the type parm is a valid receiver it type too
                        // - In general, it's valid if there's a deref chain from this type to `self` (maybe could check that in a later pass, instead of erroring here)
                        if (isValidCustomReceiver(pe->mParams.types[0])) {
                            ty = mCtx.mCrate->types.intern(mv$(data));
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
                    ty = mCtx.mCrate->types.borrow(e.type, inner);
                    return true;
                } else if (ty->is_Pointer()) {
                    const auto& e = ty->as_Pointer();
                    auto inner = e.inner;
                    if (!isValidCustomReceiver(inner)) {
                        return false;
                    }
                    ty = mCtx.mCrate->types.pointer(e.type, inner);
                    return true;
                } else {
                    return false;
                }
            }
        } ivcr(*this, sp, realSelfType);

        if (argSelfTy == mCrate->types.self() || argSelfTy == realSelfType) {
            receiver = HIRFunction::Receiver::Value;
        } else if (const auto* e = argSelfTy->opt_Borrow()) {
            if (e->inner == mCrate->types.self() || e->inner == realSelfType) {
                if (e->inner == realSelfType) {
                    argSelfTy = mCrate->types.borrow(e->type, mCrate->types.self());
                }
                switch (e->type) {
                    case HIRBorrowType::Owned:
                        receiver = HIRFunction::Receiver::BorrowOwned;
                        break;
                    case HIRBorrowType::Unique:
                        receiver = HIRFunction::Receiver::BorrowUnique;
                        break;
                    case HIRBorrowType::Shared:
                        receiver = HIRFunction::Receiver::BorrowShared;
                        break;
                }
            } else {
                auto inner = e->inner;
                if (ivcr.isValidCustomReceiver(inner)) {
                    argSelfTy = mCrate->types.borrow(e->type, inner);
                    receiver = HIRFunction::Receiver::Custom;
                }
            }
        } else if (const auto* e = argSelfTy->opt_Path()) {
            // Box - Compare with `owned_box` lang item
            if (const auto* pe = e->path.mData.opt_Generic()) {
                auto p = mCrate->getLangItemPathOpt("owned_box");
                if (pe->mPath == p) {
                    if (pe->mParams.types.size() >= 1 && (pe->mParams.types[0] == mCrate->types.self() || pe->mParams.types[0] == realSelfType)) {
                        if (pe->mParams.types[0] == realSelfType) {
                            auto data = argSelfTy->cloneData();
                            data.as_Path().path.mData.as_Generic().mParams.types[0] = mCrate->types.self();
                            argSelfTy = mCrate->types.intern(mv$(data));
                        }
                        receiver = HIRFunction::Receiver::Box;
                    }
                }
                // TODO: for other types, support arbitary structs/paths.
                if (receiver == HIRFunction::Receiver::Free) {
                    if (ivcr.isValidCustomReceiver(argSelfTy)) {
                        receiver = HIRFunction::Receiver::Custom;
                    }
                }
            }
        } else if (ivcr.isValidCustomReceiver(argSelfTy)) {
            receiver = HIRFunction::Receiver::Custom;
        } else {
        }

        if (receiver == HIRFunction::Receiver::Free) {
            ERROR(sp, E0000, "Unknown receiver type - " << argSelfTy);
        }
    }

    bool forceEmit = false;
    HIRFunction::Markings markings;
    switch (f.markings.inlineType) {
        case ASTFunction::Markings::Inline::Auto:
            markings.inlineType = HIRFunction::Markings::Inline::Auto;
            break;
        case ASTFunction::Markings::Inline::Never:
            markings.inlineType = HIRFunction::Markings::Inline::Never;
            break;
        case ASTFunction::Markings::Inline::Always:
            markings.inlineType = HIRFunction::Markings::Inline::Always;
            forceEmit = true;
            break;
        case ASTFunction::Markings::Inline::Normal:
            markings.inlineType = HIRFunction::Markings::Inline::Normal;
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
    markings.isRustcIntrinsic = attrs.has("rustc_intrinsic");
    markings.isRustcPromotable = attrs.has("rustc_promotable");

    HIRLinkage linkage;
    switch (f.markings.linkage) {
        case ASTLinkage::Default:
            break;
        case ASTLinkage::Weak:
            linkage.type = HIRLinkage::Type::Weak;
            break;
        case ASTLinkage::ExternWeak:
            BUG(sp, "Invalid linkage on function");
    }
    linkage.section = f.markings.linkSection;

    // Convert #[link_name/no_mangle] attributes into the name
    if (mAstCrate->testHarness && f.code().isValid()) {
        // If we're making a test harness, and this item defines code, don't apply the linkage rules
    } else if (f.markings.linkName != "") {
        linkage.name = f.markings.linkName;
    } else if (attrs.get("rustc_std_internal_symbol")) {
        linkage.name = p.getName();
        linkage.type = HIRLinkage::Type::Weak;
    } else if (attrs.get("no_mangle")) {
        linkage.name = p.getName();
    } else {
        // Leave linkage.name as empty
    }

    // If there's no code, mangle the name (According to the ABI) and set linkage.
    if (linkage.name == "" && !f.code().isValid()) {
        linkage.name = p.getName();
    }

    HIRFunction rv;
    rv.saveCode = forceEmit;
    rv.linkage = mv$(linkage);
    rv.receiver = receiver;
    if (receiver == HIRFunction::Receiver::Custom) {
        rv.receiverType = MonomorphiserNop(mCrate->types).monomorphType(f.args()[0].ty->span(), args.front().second, false);
        // Ensure that the reciever references `Self`
        ASSERT_BUG(
            f.args()[0].ty->span(),
            visitTyWith(
                *rv.receiverType,
                [](const HIRTypeData* v) {
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
    rv.source = SourceLocation(f.sp());
    rv.mCode = LowerHIRExpr(f.code());
    rv.markings = markings;

    if (f.isAsync()) {
        // Wrap the code in an async block
        if (rv.mCode) {
            auto* asyncNode = mCrate->pool->make<HIRExprNodeAsyncBlock>(sp, rv.returnType, rv.mCode.takeNode(), true);
            asyncNode->resType = mCrate->types.infer();
            rv.mCode = HIRExprPtr(HIRExprNodeP(asyncNode));
        }
        // Make the return type be `impl Future<Output=Ret>`
        HIRTraitPath futurePath;
        futurePath.mPath.mPath = mCrate->getLangItemPath(sp, "future_trait");
        futurePath.typeBounds.insert(std::make_pair(RcString::newInterned("Output"), HIRTraitPath::AtyEqual{futurePath.mPath.clone(), {}, std::move(rv.returnType)}));
        rv.returnType = mCrate->types.intern(HIRTypeData::make_ErasedType(HIRTypeDataErasedType{true, ::makeVec1(std::move(futurePath)), TypeDataErasedTypeInner::Data_Fcn{HIRPath(HIRSimplePath()), 0}}));
    }

    return rv;
}

void _add_mod_ns_item(stl::ObjPool& pool, HIRModule& mod, RcString name, HIRPublicity isPub, HIRTypeItem ti) {
    mod.modItems.insert(::std::make_pair(mv$(name), pool.make<HIRVisEnt<HIRTypeItem>>(HIRVisEnt<HIRTypeItem>{isPub, mv$(ti)})));
}

void _add_mod_val_item(stl::ObjPool& pool, HIRModule& mod, RcString name, HIRPublicity isPub, HIRValueItem ti) {
    mod.valueItems.insert(::std::make_pair(mv$(name), pool.make<HIRVisEnt<HIRValueItem>>(HIRVisEnt<HIRValueItem>{isPub, mv$(ti)})));
}

void _add_mod_mac_item(stl::ObjPool& pool, HIRModule& mod, RcString name, HIRPublicity isPub, HIRMacroItem ti) {
    mod.macroItems.insert(::std::make_pair(mv$(name), pool.make<HIRVisEnt<HIRMacroItem>>(HIRVisEnt<HIRMacroItem>{isPub, mv$(ti)})));
}

HIRValueItem AST2HIR::LowerHIRStatic(HIRItemPath p, const ASTAttributeList& attrs, const ASTStatic& e, const Span& sp, const RcString& name) {
    TRACE_FUNCTION_F(p);

    if (e.sClass() == ASTStatic::CONST) {
        // Note: Empty names are allowed for `const _: ...`
        return HIRValueItem::make_Constant(HIRConstant(HIRGenericParams{}, LowerHIRType(e.type()), LowerHIRExpr(e.value())));
    } else {
        // Note: Empty names are allowed for `const _: ...`
        ASSERT_BUG(sp, name != "", "Empty constant name " << p);
        HIRLinkage linkage;
        switch (e.markings.linkage) {
            case ASTLinkage::Default:
                break;
            case ASTLinkage::Weak:
                linkage.type = HIRLinkage::Type::Weak;
                break;
            case ASTLinkage::ExternWeak:
                linkage.type = HIRLinkage::Type::ExternWeak;
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

        return HIRValueItem::make_Static(HIRStatic(mv$(linkage), (e.sClass() == ASTStatic::MUT), LowerHIRType(e.type()), LowerHIRExpr(e.value())));
    }
}

HIRModule AST2HIR::LowerHIRModule(const ASTModule& astMod, HIRItemPath path, ::std::vector<HIRSimplePath> traits) {
    TRACE_FUNCTION_F("path = " << path);
    HIRModule mod{};

    mod.traits = mv$(traits);

    auto modPath = path.getSimplePath();
    auto getVis = [&](const ASTVisibility& vis) {
        return LowerHIRVis(modPath, vis);
    };

    // Populate trait list
    {
        struct Foo {
            AST2HIR& mCtx;
            HIRModule& mod;

            Foo(AST2HIR& ctx, HIRModule& mod)
                : mCtx(ctx)
                , mod(mod)
            {
            }

            void pushTrait(HIRSimplePath sp) {
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
                        pushTrait(mCtx.LowerHIRSimplePath(e.sp, *e.ent.path, FromASTPathClass::Type, true));
                    }
                }
            }

            void pushTraitAliasHir(const HIRTraitAlias& ta) {
                for (const auto& p : ta.traits) {
                    if (const auto* tap = mCtx.mCrate->getTypeitemByPath(Span(), p.mPath.mPath).opt_TraitAlias()) {
                        pushTraitAliasHir(*tap);
                    } else {
                        pushTrait(p.mPath.mPath);
                    }
                }
            }
        };

        Foo f{*this, mod};
        for (const auto& traitPath : astMod.traits) {
            f.pushTrait(HIRSimplePath((traitPath.crate == "" ? mCrateName : traitPath.crate), traitPath.nodes));
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
            auto itemPath = HIRItemPath(path, name.c_str());
            auto ti = HIRTypeItem::make_Module(LowerHIRModule(submod, itemPath, mod.traits));
            _add_mod_ns_item(*mCrate->pool, mod, mv$(name), HIRPublicity::newPriv(modPath), mv$(ti));
        }
    }

    for (const auto& ip : astMod.mItems) {
        const auto& item = *ip;
        const auto& sp = item.span;
        auto itemPath = HIRItemPath(path, item.name.c_str());
        DEBUG(itemPath << " " << item.data.tagStr());
        TU_MATCH_HDRA( (item.data), {)
        TU_ARMA(None, e) {
            }
            TU_ARMA(Macro, e) {
                // NOTE: These are in `m_macros`
            }
            TU_ARMA(MacroInv, e) {
                // Valid.
            }
            TU_ARMA(GlobalAsm, e) {
                HIRGlobalAssembly item;
                item.span = sp;
                item.lines = std::move(e.lines);
                item.operands.reserve(e.operands.size());
                for (auto& operand : e.operands) {
                    TU_MATCH_HDRA((operand), {)
                    TU_ARMA(Const, expr) {
                            auto value = LowerHIRConstGeneric(*expr);
                            ASSERT_BUG(sp, value.is_Unevaluated(), "global_asm const operand lowered without an expression");
                            const auto* type = (*value.as_Unevaluated()->expr)->resType;
                            item.operands.push_back(HIRGlobalAsmOperand::make_Const({std::move(value), type}));
                        }
                        TU_ARMA(Sym, sym) {
                            item.operands.push_back(HIRGlobalAsmOperand::make_Sym(LowerHIRPath(sp, sym, FromASTPathClass::Value)));
                        }
                    }
                }
                item.options = e.options;
                mod.globalAsm.push_back(std::move(item));
            }
            TU_ARMA(ExternBlock, e) {
                if (e.items().size() > 0) {
                    TODO(sp, "Expand ExternBlock");
                }
                for (const auto& lib : e.libraries) {
                    mCrate->extLibs.push_back(HIRExternLibrary{lib.libName});
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
                _add_mod_ns_item(*mCrate->pool, mod, item.name, getVis(item.vis), LowerHIRModule(e, mv$(itemPath)));
            }
            TU_ARMA(Crate, e) {
                // All 'extern crate' items should be normalised into a list in the crate root
                // - If public, add a namespace import here referring to the root of the imported crate
                _add_mod_ns_item(*mCrate->pool, mod, item.name, getVis(item.vis), HIRTypeItem::make_Import({HIRSimplePath(e.name, {}), false, 0}));
            }
            TU_ARMA(Type, e) {
                if (e.type()->mData.is_Any()) {
                    if (!e.params().mParams.empty() || !e.params().bounds.empty()) {
                        ERROR(item.span, E0000, "Generics on extern type");
                    }
                    _add_mod_ns_item(*mCrate->pool, mod, item.name, getVis(item.vis), HIRExternType{});
                    break;
                }
                _add_mod_ns_item(*mCrate->pool, mod, item.name, getVis(item.vis), HIRTypeItem::make_TypeAlias(LowerHIRTypeAlias(itemPath, e)));
            }
            TU_ARMA(Struct, e) {
                /// Add value reference
                if (e.mData.is_Unit()) {
                    _add_mod_val_item(*mCrate->pool, mod, item.name, getVis(item.vis), HIRValueItem::make_StructConstant({itemPath.getSimplePath()}));
                } else if (e.mData.is_Tuple()) {
                    _add_mod_val_item(*mCrate->pool, mod, item.name, getVis(item.vis), HIRValueItem::make_StructConstructor({itemPath.getSimplePath()}));
                } else {
                }
                _add_mod_ns_item(*mCrate->pool, mod, item.name, getVis(item.vis), LowerHIRStruct(ip->span, itemPath, e, item.attrs, mod));
            }
            TU_ARMA(Enum, e) {
                auto enm = LowerHIREnum(itemPath, e, item.attrs, [&](auto name, auto str) {
                    _add_mod_ns_item(*mCrate->pool, mod, name, getVis(item.vis), mv$(str));
                }, mod);
                _add_mod_ns_item(*mCrate->pool, mod, item.name, getVis(item.vis), mv$(enm));
            }
            TU_ARMA(Union, e) {
                _add_mod_ns_item(*mCrate->pool, mod, item.name, getVis(item.vis), LowerHIRUnion(itemPath, e, item.attrs));
            }
            TU_ARMA(Trait, e) {
                _add_mod_ns_item(*mCrate->pool, mod, item.name, getVis(item.vis), LowerHIRTrait(itemPath.getSimplePath(), e, item.attrs));
            }
            TU_ARMA(TraitAlias, e) {
                _add_mod_ns_item(*mCrate->pool, mod, item.name, getVis(item.vis), LowerHIRTraitAlias(sp, itemPath, e));
            }
            TU_ARMA(Function, e) {
                _add_mod_val_item(*mCrate->pool, mod, item.name, getVis(item.vis), LowerHIRFunction(itemPath, item.attrs, e, HIRTypeRef{}));
            }
            TU_ARMA(Static, e) {
                _add_mod_val_item(*mCrate->pool, mod, item.name, getVis(item.vis), LowerHIRStatic(itemPath, item.attrs, e, sp, item.name));
            }
        }
    }
    // Some explicit handling of mac
    for (auto& mac : const_cast<ASTModule&>(astMod).macros()) {
        if (mac.data || mac.vis.isGlobal()) {
            ASSERT_BUG(mac.span, mac.data, "Null macro - " << mac.name);
            ASSERT_BUG(mac.span, mac.data->rules.size() > 0, "Empty macro - " << mac.name);
            _add_mod_mac_item(*mCrate->pool, mod, mac.name, getVis(mac.vis), std::move(mac.data));
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
            HIRTypeItem ti;
            if (const auto* pb = ie.second.path.mBindings.type.binding.opt_EnumVar()) {
                DEBUG("Import NS " << ie.first << " = " << hirPath << " (Enum Variant)");
                ti = HIRTypeItem::make_Import({mv$(hirPath), true, pb->idx});
            } else {
                DEBUG("Import NS " << ie.first << " = " << hirPath);
                ti = HIRTypeItem::make_Import({mv$(hirPath), false, 0});
            }
            _add_mod_ns_item(*mCrate->pool, mod, ie.first, getVis(ie.second.vis), mv$(ti));
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
            HIRValueItem vi;

            TU_MATCH_HDRA( (ie.second.path.mBindings.value.binding), {)
            default:
                DEBUG("Import VAL " << ie.first << " = " << hirPath);
                vi = HIRValueItem::make_Import({mv$(hirPath), false, 0});
                TU_ARMA(EnumVar, pb) {
                    DEBUG("Import VAL " << ie.first << " = " << hirPath << " (Enum Variant)");
                    vi = HIRValueItem::make_Import({mv$(hirPath), true, pb.idx});
                }
            }
            _add_mod_val_item(*mCrate->pool, mod, ie.first, getVis(ie.second.vis), mv$(vi));
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
            auto mi = HIRMacroItem::make_Import({mv$(hirPath)});
            _add_mod_mac_item(*mCrate->pool, mod, ie.first, getVis(ie.second.vis), mv$(mi));
        } else {
            DEBUG("Defined MACRO " << ie.first << " = " << hirPath);
        }
    }

    return mod;
}

void AST2HIR::LowerHIRModuleImpls(const ASTModule& astMod, HIRCrate& hirCrate) {
    TRACE_FUNCTION_F(astMod.path());
    HIRSimplePath modPath(mCrateName, astMod.path().nodes);

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

                HIRItemPath path(type, traitName, traitArgs);
                DEBUG("path = " << path);

                ::std::map<RcString, HIRTraitImpl::ImplEnt<HIRFunction>> methods;
                ::std::map<RcString, HIRTraitImpl::ImplEnt<HIRConstant>> constants;
                ::std::map<RcString, HIRTraitImpl::ImplEnt<HIRTypeRef>> types;

                for (const auto& item : impl.items()) {
                    HIRItemPath itemPath(path, item.name.c_str());
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
                                constants.insert(::std::make_pair(item.name, HIRTraitImpl::ImplEnt<HIRConstant>{item.isSpecialisable, HIRConstant(HIRGenericParams{}, LowerHIRType(e.type()), LowerHIRExpr(e.value()))}));
                            } else {
                                TODO(item.sp, "Associated statics in trait impl");
                            }
                        }
                        TU_ARMA(Type, e) {
                            DEBUG("- type " << item.name);
                            auto atyParams = LowerHIRGenericParams(e.params(), nullptr);
                            //ASSERT_BUG(Span(), aty_params.is_empty(), "TODO: GATs");

                            assert(!mImplTraitSource.path);
                            HIRItemPath ip1(modPath);
                            ::std::string name2 = ::std::string("#impl_") + ::std::to_string((uintptr_t)&impl) + "_" + item.name.c_str();
                            HIRItemPath ip2(ip1, name2.c_str());
                            mImplTraitSource = ImplTraitSource(&ip2, &params, &atyParams);

                            types.insert(::std::make_pair(item.name, HIRTraitImpl::ImplEnt<HIRTypeRef>{item.isSpecialisable, LowerHIRType(e.type())}));

                            mImplTraitSource = ImplTraitSource();
                        }
                        TU_ARMA(Function, e) {
                            DEBUG("- method " << item.name);
                            auto fcn = LowerHIRFunction(itemPath, item.attrs, e, type);
                            if (impl.def().isConst()) {
                                fcn.isConst = true;
                            }
                            methods.insert(::std::make_pair(item.name, HIRTraitImpl::ImplEnt<HIRFunction>{item.isSpecialisable, mv$(fcn)}));
                        }
                    }
                }

                // Sorted later on
                auto hirImpl = ::std::make_unique<HIRTraitImpl>(HIRTraitImpl{
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
            } else if (impl.def().type()->mData.is_None()) {
                // Ignore - These are encoded in the 'is_marker' field of the trait
            } else {
                auto type = LowerHIRType(impl.def().type());
                hirCrate.markerImpls[mv$(traitName)].generic.push_back(box$(
                    HIRMarkerImpl{
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
            HIRItemPath path(type);

            auto getVis = [&](const ASTVisibility& vis) {
                return LowerHIRVis(modPath, vis);
            }; // TODO: Does this need to consume anon modules?

            ::std::map<RcString, HIRTypeImpl::VisImplEnt<HIRFunction>> methods;
            ::std::map<RcString, HIRTypeImpl::VisImplEnt<HIRConstant>> constants;
            ::std::map<RcString, HIRTypeImpl::VisImplEnt<HIRTypeAlias>> types;

            for (const auto& item : impl.items()) {
                HIRItemPath itemPath(path, item.name.c_str());
                TU_MATCH_HDRA( (*item.data), {)
                default:
                    BUG(item.sp, "Unexpected item type in inherent impl - " << item.data->tagStr());
                    TU_ARMA(None, e) {
                    }
                    TU_ARMA(MacroInv, e) {
                    }
                    TU_ARMA(Static, e) {
                        if (e.sClass() == ASTStatic::CONST) {
                            constants.insert(::std::make_pair(item.name, HIRTypeImpl::VisImplEnt<HIRConstant>{getVis(item.vis), item.isSpecialisable, HIRConstant(HIRGenericParams{}, LowerHIRType(e.type()), LowerHIRExpr(e.value()))}));
                        } else {
                            TODO(item.sp, "Associated statics in inherent impl");
                        }
                    }
                    TU_ARMA(Type, e) {
                        DEBUG("- type " << item.name);
                        auto atyParams = LowerHIRGenericParams(e.params(), nullptr);

                        assert(!mImplTraitSource.path);
                        mImplTraitSource = ImplTraitSource(&itemPath, &params, &atyParams);
                        auto atyType = LowerHIRType(e.type());
                        mImplTraitSource = ImplTraitSource();

                        types.insert(::std::make_pair(item.name, HIRTypeImpl::VisImplEnt<HIRTypeAlias>{getVis(item.vis), item.isSpecialisable, HIRTypeAlias{mv$(atyParams), mv$(atyType)}}));
                    }
                    TU_ARMA(Function, e) {
                        methods.insert(::std::make_pair(item.name, HIRTypeImpl::VisImplEnt<HIRFunction>{getVis(item.vis), item.isSpecialisable, LowerHIRFunction(itemPath, item.attrs, e, type)}));
                    }
                }
            }

            // Sorted later on
            hirCrate.typeImpls.generic.push_back(box$(
                HIRTypeImpl{
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
            HIRMarkerImpl{
                mv$(params),
                mv$(traitArgs),
                false,
                mv$(type),

                modPath
            }
        ));
    }
}

class IndexVisitor: public HIRVisitor {
    const HIRCrate& crate;
    Span nullSpan;

public:
    IndexVisitor(const HIRCrate& crate)
        : HIRVisitor(nullptr, crate.types)
        , crate(crate)
    {
    }

    void visitParams(HIRGenericParams& params) override {
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
HIRCrate* LowerHIRFromAST(const WireBoard& wb, stl::ObjPool* pool, ASTCrate& crate) {
    AST2HIR self;
    return self.lowerCrate(wb, pool, crate);
}
HIRCrate* AST2HIR::lowerCrate(const WireBoard& wb, stl::ObjPool* pool, ASTCrate& crate) {
    mWb = &wb;
    auto& rv = *pool->make<HIRCrate>(pool, crate.types);

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

    mCrate = &rv;
    mAstCrate = &crate;
    mCrateName = rv.crateName;
    mCoreCrate = crate.extCratenameCore;
    wb.settings->crateName = mCrateName;
    wb.settings->coreCrate = mCoreCrate;
    auto macros = std::map<RcString, HIRMacroItem>();

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
                    HIRMacroItem mi;
                    if (&mod == &crate.mRootModule) {
                        mi = mv$(mac.data);
                    } else {
                        assert(mac.data);
                        assert(!mac.data->rules.empty());
                        auto pc = mod.path().nodes;
                        pc.push_back(mac.name);
                        mi = HIRMacroItem::make_Import({HIRSimplePath(mCrateName, std::move(pc))});
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

                    for (auto& e : macros) {
                        if (e.second.is_MacroRules()) {
                            ASSERT_BUG(Span(), !e.second.as_MacroRules()->rules.empty(), "Empty macro? - " << e.first);
                        }
                    }
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
                auto path = HIRSimplePath(mac.path.crate == "" ? mCrateName : mac.path.crate, mac.path.nodes);
                auto res = macros.insert(std::make_pair(mac.name, HIRMacroItem::make_Import({path})));
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
                static HIRProcMacro::Ty cvtMacroTy(ASTProcMacroTy ast) {
                    switch (ast) {
                        case ASTProcMacroTy::Function:
                            return HIRProcMacro::Ty::Function;
                        case ASTProcMacroTy::Derive:
                            return HIRProcMacro::Ty::Derive;
                        case ASTProcMacroTy::Attribute:
                            return HIRProcMacro::Ty::Attribute;
                    }
                    throw "Invalid AST macro type";
                }
            };

            // Register under an invalid SimplePath
            HIRProcMacro::Ty ty = H::cvtMacroTy(ent.ty);
            macros.insert(std::make_pair(ent.name, HIRProcMacro{ty, ent.name, HIRSimplePath(RcString(""), {ent.name}), ent.attributes}));
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
        rv.mLangItems.insert(::std::make_pair(langItemPath.first, HIRSimplePath(mCrateName, langItemPath.second.nodes)));
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
        rv.extCrates.insert(::std::make_pair(extCrate.first, HIRExternCrate{extCrate.second.hir, crateFile, extCrate.second.filename}));
    }
    pathSized = rv.getLangItemPathOpt("sized");
    pathPointeeSized = rv.getLangItemPathOpt("pointee_sized");
    pathMetadataSized = rv.getLangItemPathOpt("metadata_sized");

    rv.mRootModule = LowerHIRModule(crate.mRootModule, HIRItemPath(rv.crateName));
    for (auto& e : macros) {
        if (e.second.is_MacroRules()) {
            ASSERT_BUG(Span(), !e.second.as_MacroRules()->rules.empty(), "Empty macro? - " << e.first);
        }
        rv.mRootModule.macroItems.insert(::std::make_pair(e.first, rv.pool->make<HIRVisEnt<HIRMacroItem>>(HIRVisEnt<HIRMacroItem>{HIRPublicity::newGlobal(), mv$(e.second)})));
    }

    LowerHIRModuleImpls(crate.mRootModule, rv);

    // Set all pointers in the HIR to the correct (now fixed) locations

    // Macro fixups:
    // - Convert interpolated AST items to token sequences
    {
        struct H {
            AST2HIR& mCtx;
            explicit H(AST2HIR& ctx) : mCtx(ctx) {}
            void fixMacroContents(std::vector<MacroExpansionEnt>& ruleContents) {
                for (auto it = ruleContents.begin(); it != ruleContents.end();) {
                    if (auto* tok = it->opt_Token()) {
                        //TODO: Can this share with `proc_macro`? Maybe a function on AST types to generate a token tree from the AST again.
                        struct NewToks {
                            const WireBoard& wb;
                            std::vector<MacroExpansionEnt> out;

                            void emitFromString(const std::string& s) {
                                ::std::istringstream iss{s};
                                Lexer l{*wb.pool, iss, ASTEdition::Rust2021, {}};
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

                            void emitType(::ASTType*& ty) {
                                TU_MATCH_HDRA( (ty->mData), { )
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

                        NewToks newToks{*mCtx.mWb};
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

            void fixMacrosInMod(HIRItemPath path, HIRModule& mod) {
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
                            mr.sourceCrate = mCtx.mCrateName;
                        }
                        for (auto& rule : mr.rules) {
                            fixMacroContents(rule.contents);
                        }
                    }
                    if (const auto* i = mi.second->ent.opt_Import()) {
                        DEBUG(path << ": Import " << mi.first << " = " << i->path);
                        if (i->path.crateName() == CRATE_BUILTINS) {
                        } else if (const auto* i2 = mCtx.mCrate->getMacroitemByPath(Span(), i->path).opt_Import()) {
                            BUG(Span(), "Attempted recusive import - " << i->path << " points at " << i2->path);
                        }
                    }
                }
            }
        };

        H(*this).fixMacrosInMod(HIRItemPath(""), rv.mRootModule);
    }

    if (mCoreCrate == "") {
        mCoreCrate = mCrateName;
        wb.settings->coreCrate = mCoreCrate;
    }

    mCrate = nullptr;
    return &rv;
}

struct LowerHIRExprNodeVisitor: public ASTNodeVisitor {
    AST2HIR& mCtx;
    explicit LowerHIRExprNodeVisitor(AST2HIR& ctx) : mCtx(ctx) {}

    HIRExprNodeP mRv;

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
                targetHygiene.leaveMacroDefinition(*mCtx.mCrate->pool, definition.definitionId, definition.tokenHygiene, definition.definitionHygiene);
            }
            if (it->source.name == target.name && it->source.hygiene.isVisible(targetHygiene)) {
                return it->lowered;
            }
        }
        ERROR(sp, E0000, "Could not find loop label '" << target.name);
    }

    HIRExprNodeP lower(ASTExprNodeP& ep) {
        assert(ep);
        ep->visit(*this);
        ASSERT_BUG(ep->span(), mRv, ep.typeName() << " - Yielded a nullptr HIR node");
        mRv->resType = mCtx.mCrate->types.infer();
        return std::move(mRv);
    }

    HIRExprNodeP lowerOpt(ASTExprNodeP& ep) {
        if (ep) {
            return lower(ep);
        } else {
            return nullptr;
        }
    }

    HIRExprNodeP lowerIsolated(ASTExprNodeP& ep) {
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
        auto rv = mCtx.mCrate->pool->make<HIRExprNodeBlock>(v.span());
        bool lastHasSemicolon = true;
        for (auto& n : v.nodes) {
            ASSERT_BUG(v.span(), n.node, "NULL node encountered in block");
            if (const auto* definition = cast<ASTExprNodeMacroDefinition>(n.node.get())) {
                macroDefinitions.push_back(MacroDefinition{definition->definitionId, definition->tokenHygiene, definition->definitionHygiene});
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
            rv->localMod = HIRSimplePath(mCtx.mCrateName, v.localMod->path().nodes);
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
                auto* breakNode = mCtx.mCrate->pool->make<HIRExprNodeLoopControl>(v.span(), label, /*cont=*/false, ::std::move(rv->valueNode));
                rv->nodes.push_back(HIRExprNodeP(breakNode));
                rv->valueNode.reset();
            }
            auto* loop = mCtx.mCrate->pool->make<HIRExprNodeLoop>(v.span(), label, HIRExprNodeP(rv));
            loop->requireLabel = true;
            mRv.reset(loop);
        } else {
            mRv.reset(static_cast<HIRExprNode*>(rv));
        }

        switch (v.blockType) {
            case ASTExprNodeBlock::Type::Bare:
                break;
            case ASTExprNodeBlock::Type::Unsafe:
                break;
            case ASTExprNodeBlock::Type::Const:
                mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeConstBlock>(v.span(), std::move(mRv)));
                break;
        }
    }

    virtual void visit(ASTExprNodeAsyncBlock& v) override {
        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeAsyncBlock>(v.span(), mCtx.mCrate->types.infer(), lowerIsolated(v.inner), v.isMove));
    }

    virtual void visit(ASTExprNodeGeneratorBlock& v) override {
        // TODO: Wrap with something that provides an impl of Iterator
        // - `::core::iter::from_coroutine`
        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeGenerator>(v.span(), mCtx.mCrate->types.infer(), mCtx.mCrate->types.infer(), mCtx.mCrate->types.infer(), lowerIsolated(v.inner), v.isMove, false));
        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeCallPath>(v.span(), HIRSimplePath(mCtx.mCoreCrate, {"iter", "sources", "from_coroutine", "from_coroutine"}), makeVec1(mv$(mRv))));
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
        ::std::vector<HIRExprNodeAsm::ValRef> outputs;
        ::std::vector<HIRExprNodeAsm::ValRef> inputs;
        for (auto& vr : v.output) {
            outputs.push_back(HIRExprNodeAsm::ValRef{vr.name, lower(vr.value)});
        }
        for (auto& vr : v.input) {
            inputs.push_back(HIRExprNodeAsm::ValRef{vr.name, lower(vr.value)});
        }

        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeAsm>(v.span(), v.text, mv$(outputs), mv$(inputs), v.clobbers, v.flags));
    }

    virtual void visit(ASTExprNodeAsm2& v) override {
        std::vector<HIRExprNodeAsm2::Param> params;
        for (auto& p : v.mParams) {
            TU_MATCH_HDRA((p), {)
            TU_ARMA(Const, e) {
                    ASSERT_BUG(v.span(), e, "Missing node for ASM Const");
                    params.push_back(lower(e));
                }
                TU_ARMA(Sym, e) {
                    params.push_back(mCtx.LowerHIRPath(v.span(), e, FromASTPathClass::Value));
                }
                TU_ARMA(RegSingle, e) {
                    params.push_back(
                        HIRExprNodeAsm2::Param::make_RegSingle({
                            e.dir,
                            e.spec.clone(),
                            e.val ? lower(e.val) : nullptr // e.g. `lateout(regname) _`
                        })
                    );
                }
                TU_ARMA(Reg, e) {
                    params.push_back(HIRExprNodeAsm2::Param::make_Reg({e.dir, e.spec.clone(), e.valIn ? lower(e.valIn) : nullptr, e.valOut ? lower(e.valOut) : nullptr}));
                }
            }
        }
        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeAsm2>(v.span(), v.options, v.lines, mv$(params)));
    }

    virtual void visit(ASTExprNodeFlow& v) override {
        switch (v.mType) {
            case ASTExprNodeFlow::RETURN:
                if (v.mValue) {
                    mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeReturn>(v.span(), lower(v.mValue)));
                } else {
                    mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeReturn>(v.span(), HIRExprNodeP(mCtx.mCrate->pool->make<HIRExprNodeTuple>(v.span(), ::std::vector<HIRExprNodeP>{}))));
                }
                break;
            case ASTExprNodeFlow::YIELD:
                mHasYield = true;
                {
                    auto value = v.mValue ? lower(v.mValue) : HIRExprNodeP(mCtx.mCrate->pool->make<HIRExprNodeTuple>(v.span(), ::std::vector<HIRExprNodeP>{}));
                    mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeYield>(v.span(), std::move(value)));
                }
                break;
            case ASTExprNodeFlow::CONTINUE:
            case ASTExprNodeFlow::BREAK: {
                auto val = v.mValue ? lower(v.mValue) : HIRExprNodeP();
                ASSERT_BUG(v.span(), !(v.mType == ASTExprNodeFlow::CONTINUE && val), "Continue with a value isn't allowed");
                auto target = resolveLoopLabel(v.span(), v.target);
                mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeLoopControl>(v.span(), mv$(target), (v.mType == ASTExprNodeFlow::CONTINUE), mv$(val)));
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
            auto pat = mCtx.LowerHIRPattern(v.pat);
            auto type = mCtx.LowerHIRType(v.mType);
            auto nodeValue = lower(v.mValue);
            auto nodeElse = lower(v.elseNode);

            auto base = v.letelseSlots.first;
            auto count = v.letelseSlots.second;
            DEBUG(pat);

            struct V: public HIRVisitor {
                unsigned base;
                unsigned count;
                std::vector<HIRPatternBinding> bindings;
                std::map<unsigned, unsigned> mapping;

                V(HIRTypeInterner& types, unsigned base, unsigned count)
                    : HIRVisitor(nullptr, types)
                    , base(base)
                    , count(count)
                {
                }

                void visitPattern(HIRPattern& pat) override {
                    HIRVisitor::visitPattern(pat);
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
                    //    }
                    //}
                }

                void handleBinding(HIRPatternBinding& pb) {
                    auto it = mapping.find(pb.slot);
                    if (it == mapping.end()) {
                        ASSERT_BUG(Span(), bindings.size() < this->count, "Miscount of variables in `let-else` - only allocated " << this->count);
                        unsigned newIdx = base + bindings.size();

                        bindings.push_back(HIRPatternBinding(pb));
                        bindings.back().mType = HIRPatternBinding::Type::Move;
                        it = mapping.insert(std::make_pair(pb.slot, newIdx)).first;
                    }
                    pb.isMutable = false;
                    pb.slot = it->second;
                }
            } visitor(mCtx.mCrate->types, base, count);

            visitor.visitPattern(pat);
            /*
             * ```
             * let (a,b,c,...) = match $value: $ty {
             *     $pat => (a,b,c,...),
             *     _ => { let _: ! = $else; },
             *     };
             * ```
             */
            std::vector<HIRPattern> newPats;
            std::vector<HIRExprNodeP> tupleVals;
            const auto bindingSlots = patternBindingSlots(pat, HIRPatternBindingOrder::FirstCandidate);
            ASSERT_BUG(v.span(), bindingSlots.size() == visitor.bindings.size(), "let-else candidate omitted bindings");
            for (const auto slot : bindingSlots) {
                ASSERT_BUG(v.span(), base <= slot && slot - base < visitor.bindings.size(), "Invalid temporary let-else binding " << slot);
                auto& binding = visitor.bindings[slot - base];
                tupleVals.push_back(HIRExprNodeP(mCtx.mCrate->pool->make<HIRExprNodeVariable>(v.span(), binding.mName, slot)));
                newPats.push_back(HIRPattern(std::move(binding), HIRPattern::Data{}));
            }

            std::vector<HIRExprNodeMatch::Arm> matchArms(2);
            // `$pat => (a,b,c,...),`
            matchArms[0].patterns.push_back(std::move(pat));
            matchArms[0].mCode.reset(mCtx.mCrate->pool->make<HIRExprNodeTuple>(v.span(), std::move(tupleVals)));
            matchArms[1].patterns.push_back(HIRPattern());
            // `_ => loop { let _: ! = $else; },
            matchArms[1].mCode.reset(mCtx.mCrate->pool->make<HIRExprNodeLet>(v.span(), HIRPattern(), mCtx.mCrate->types.diverge(), std::move(nodeElse)));
            matchArms[1].mCode.reset(mCtx.mCrate->pool->make<HIRExprNodeLoop>(v.span(), "", std::move(matchArms[1].mCode), /*require_label*/ true));
            // HACK: Just use the code as-is.
            // `match $value: $ty {`
            auto matchValue = type->is_Infer() // Only emit the `: $ty` part if the type was specified (not a `_`)
                                  ? std::move(nodeValue)
                                  : HIRExprNodeP(mCtx.mCrate->pool->make<HIRExprNodeUnsize>(v.span(), std::move(nodeValue), std::move(type)));
            auto match = HIRExprNodeP(mCtx.mCrate->pool->make<HIRExprNodeMatch>(v.span(), std::move(matchValue), std::move(matchArms), true));

            // `let (a,b,c,...) = ...`
            mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeLet>(v.span(), HIRPattern(::std::vector<HIRPatternBinding>(), HIRPattern::Data::make_Tuple({std::move(newPats)})), mCtx.mCrate->types.infer(), std::move(match)));
        } else {
            mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeLet>(v.span(), mCtx.LowerHIRPattern(v.pat), mCtx.LowerHIRType(v.mType), lowerOpt(v.mValue), v.isSuper));
        }
    }

    virtual void visit(ASTExprNodeAssign& v) override {
        struct H {
            static HIRExprNodeAssign::Op getOp(ASTExprNodeAssign::Operation o) {
                switch (o) {
                    case ASTExprNodeAssign::NONE:
                        return HIRExprNodeAssign::Op::None;
                    case ASTExprNodeAssign::ADD:
                        return HIRExprNodeAssign::Op::Add;
                    case ASTExprNodeAssign::SUB:
                        return HIRExprNodeAssign::Op::Sub;

                    case ASTExprNodeAssign::MUL:
                        return HIRExprNodeAssign::Op::Mul;
                    case ASTExprNodeAssign::DIV:
                        return HIRExprNodeAssign::Op::Div;
                    case ASTExprNodeAssign::MOD:
                        return HIRExprNodeAssign::Op::Mod;

                    case ASTExprNodeAssign::AND:
                        return HIRExprNodeAssign::Op::And;
                    case ASTExprNodeAssign::OR:
                        return HIRExprNodeAssign::Op::Or;
                    case ASTExprNodeAssign::XOR:
                        return HIRExprNodeAssign::Op::Xor;

                    case ASTExprNodeAssign::SHR:
                        return HIRExprNodeAssign::Op::Shr;
                    case ASTExprNodeAssign::SHL:
                        return HIRExprNodeAssign::Op::Shl;
                }
                throw "";
            }
        };

        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeAssign>(v.span(), H::getOp(v.op), lower(v.slot), lower(v.mValue)));
    }

    virtual void visit(ASTExprNodeBinOp& v) override {
        HIRExprNodeBinOp::Op op;
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
                mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeEmplace>(v.span(), HIRExprNodeEmplace::Type::Placer, lower(v.left), lower(v.right)));
                break;

            case ASTExprNodeBinOp::CMPEQU:
                op = HIRExprNodeBinOp::Op::CmpEqu;
                if (0) {
                    case ASTExprNodeBinOp::CMPNEQU:
                        op = HIRExprNodeBinOp::Op::CmpNEqu;
                }
                if (0) {
                    case ASTExprNodeBinOp::CMPLT:
                        op = HIRExprNodeBinOp::Op::CmpLt;
                }
                if (0) {
                    case ASTExprNodeBinOp::CMPLTE:
                        op = HIRExprNodeBinOp::Op::CmpLtE;
                }
                if (0) {
                    case ASTExprNodeBinOp::CMPGT:
                        op = HIRExprNodeBinOp::Op::CmpGt;
                }
                if (0) {
                    case ASTExprNodeBinOp::CMPGTE:
                        op = HIRExprNodeBinOp::Op::CmpGtE;
                }
                if (0) {
                    case ASTExprNodeBinOp::BOOLAND:
                        op = HIRExprNodeBinOp::Op::BoolAnd;
                }
                if (0) {
                    case ASTExprNodeBinOp::BOOLOR:
                        op = HIRExprNodeBinOp::Op::BoolOr;
                }
                if (0) {
                    case ASTExprNodeBinOp::BITAND:
                        op = HIRExprNodeBinOp::Op::And;
                }
                if (0) {
                    case ASTExprNodeBinOp::BITOR:
                        op = HIRExprNodeBinOp::Op::Or;
                }
                if (0) {
                    case ASTExprNodeBinOp::BITXOR:
                        op = HIRExprNodeBinOp::Op::Xor;
                }
                if (0) {
                    case ASTExprNodeBinOp::MULTIPLY:
                        op = HIRExprNodeBinOp::Op::Mul;
                }
                if (0) {
                    case ASTExprNodeBinOp::DIVIDE:
                        op = HIRExprNodeBinOp::Op::Div;
                }
                if (0) {
                    case ASTExprNodeBinOp::MODULO:
                        op = HIRExprNodeBinOp::Op::Mod;
                }
                if (0) {
                    case ASTExprNodeBinOp::ADD:
                        op = HIRExprNodeBinOp::Op::Add;
                }
                if (0) {
                    case ASTExprNodeBinOp::SUB:
                        op = HIRExprNodeBinOp::Op::Sub;
                }
                if (0) {
                    case ASTExprNodeBinOp::SHR:
                        op = HIRExprNodeBinOp::Op::Shr;
                }
                if (0) {
                    case ASTExprNodeBinOp::SHL:
                        op = HIRExprNodeBinOp::Op::Shl;
                }

                mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeBinOp>(v.span(), op, lower(v.left), lower(v.right)));
                break;
        }
    }

    virtual void visit(ASTExprNodeUniOp& v) override {
        HIRExprNodeUniOp::Op op;
        switch (v.mType) {
            case ASTExprNodeUniOp::BOX: {
                mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeEmplace>(v.span(), HIRExprNodeEmplace::Type::Boxer, HIRExprNodeP(mCtx.mCrate->pool->make<HIRExprNodeTuple>(v.span(), ::std::vector<HIRExprNodeP>{})), lower(v.mValue)));
            } break;
            case ASTExprNodeUniOp::QMARK:
                BUG(v.span(), "Encounterd question mark operator (should have been expanded in AST)");
                break;

            case ASTExprNodeUniOp::REF:
                mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeBorrow>(v.span(), HIRBorrowType::Shared, lower(v.mValue)));
                break;
            case ASTExprNodeUniOp::RawBorrow:
                mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeRawBorrow>(v.span(), HIRBorrowType::Shared, lower(v.mValue)));
                break;
            case ASTExprNodeUniOp::REFMUT:
                mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeBorrow>(v.span(), HIRBorrowType::Unique, lower(v.mValue)));
                break;
            case ASTExprNodeUniOp::RawBorrowMut:
                mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeRawBorrow>(v.span(), HIRBorrowType::Unique, lower(v.mValue)));
                break;

            case ASTExprNodeUniOp::AWait:
                mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeAWait>(v.span(), lower(v.mValue)));
                break;

            case ASTExprNodeUniOp::INVERT:
                op = HIRExprNodeUniOp::Op::Invert;
                if (0) {
                    case ASTExprNodeUniOp::NEGATE:
                        op = HIRExprNodeUniOp::Op::Negate;
                }
                mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeUniOp>(v.span(), op, lower(v.mValue)));
                break;
        }
    }

    virtual void visit(ASTExprNodeCast& v) override {
        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeCast>(v.span(), lower(v.mValue), mCtx.LowerHIRType(v.mType)));
    }

    virtual void visit(ASTExprNodeTypeAnnotation& v) override {
        // TODO: Create a proper node for this
        // - Using `Unsize` works pretty well, but isn't quite "correct"
        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeUnsize>(v.span(), lower(v.mValue), mCtx.LowerHIRType(v.mType)));
    }

    virtual void visit(ASTExprNodeCallPath& v) override {
        ::std::vector<HIRExprNodeP> args;
        for (auto& arg : v.mArgs) {
            args.push_back(lower(arg));
        }

        if (const auto* e = v.mPath.cls.opt_Local()) {
            mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeCallValue>(v.span(), HIRExprNodeP(mCtx.mCrate->pool->make<HIRExprNodeVariable>(v.span(), e->name, v.mPath.mBindings.value.binding.as_Variable().slot)), mv$(args)));
        } else {
            TU_MATCH_HDRA( (v.mPath.mBindings.value.binding), {)
            default:
                mRv.reset( mCtx.mCrate->pool->make<HIRExprNodeCallPath>( v.span(),
                    mCtx.LowerHIRPath(v.span(), v.mPath, FromASTPathClass::Value),
                    mv$( args )
                    ) );
                TU_ARMA(Static, e) {
                    bool isConst = e.static_ ? e.static_->sClass() == ASTStatic::Class::CONST : (e.hir ? false : true) // If HIR Pointer is null, this is a HIR::Const
                        ;
                    mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeCallValue>(v.span(), HIRExprNodeP(mCtx.mCrate->pool->make<HIRExprNodePathValue>(v.span(), mCtx.LowerHIRPath(v.span(), v.mPath, FromASTPathClass::Value), isConst ? HIRExprNodePathValue::CONSTANT : HIRExprNodePathValue::STATIC)), mv$(args)));
                }
                //    TODO(v.span(), "CallPath -> TupleVariant TypeAlias");
                //    }
                TU_ARMA(EnumVar, e) {
                    mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeTupleVariant>(v.span(), mCtx.LowerHIRGenericPath(v.span(), v.mPath, FromASTPathClass::Value), false, mv$(args)));
                }
                TU_ARMA(Struct, e) {
                    mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeTupleVariant>(v.span(), mCtx.LowerHIRGenericPath(v.span(), v.mPath, FromASTPathClass::Value), true, mv$(args)));
                }
            }
        }
    }

    virtual void visit(ASTExprNodeCallMethod& v) override {
        ::std::vector<HIRExprNodeP> args;
        for (auto& arg : v.mArgs) {
            args.push_back(lower(arg));
        }

        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeCallMethod>(v.span(), lower(v.val), v.method.name(), mCtx.LowerHIRPathParams(v.span(), v.method.args(), /*allow_assoc=*/false), mv$(args)));
    }

    virtual void visit(ASTExprNodeCallObject& v) override {
        ::std::vector<HIRExprNodeP> args;
        for (auto& arg : v.mArgs) {
            args.push_back(lower(arg));
        }

        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeCallValue>(v.span(), lower(v.val), mv$(args)));
    }

    virtual void visit(ASTExprNodeLoop& v) override {
        auto label = enterLoopLabel(v.label);
        auto code = lower(v.mCode);
        leaveLoopLabel(label);
        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeLoop>(v.span(), mv$(label), mv$(code)));
    }

    void visit(ASTExprNodeFor& v) override {
        // NOTE: This should already be desugared (as a pass before resolve)
        BUG(v.span(), "Encountered still-sugared for loop");
    }

    ::std::vector<HIRExprNodeMatch::Guard> ifletToGuards(std::vector<ASTIfLetCondition>& guards) {
        ::std::vector<HIRExprNodeMatch::Guard> rv;
        rv.reserve(guards.size());
        for (auto& c : guards) {
            auto condPat = c.optPat ? mCtx.LowerHIRPattern(*c.optPat) : HIRPattern{HIRPatternBinding(), HIRPattern::Data::make_Value({HIRPattern::Value::make_Integer({HIRCoreType::Bool, U128(1)})})};
            auto condVal = lowerOpt(c.value);
            rv.push_back(HIRExprNodeMatch::Guard{std::move(condPat), std::move(condVal), c.optPat ? false : true});
        }
        return rv;
    }

    virtual void visit(ASTExprNodeWhile& v) override {
        // Desugar to `loop { match () { _ if ... => { body }, _ => break, } }`
        auto label = enterLoopLabel(v.label);
        ::std::vector<HIRExprNodeMatch::Arm> arms;
        arms.push_back(HIRExprNodeMatch::Arm{makeVec1(HIRPattern()), ifletToGuards(v.conditions), lower(v.mCode)});
        arms.push_back(HIRExprNodeMatch::Arm{makeVec1(HIRPattern()), {}, HIRExprNodeP(mCtx.mCrate->pool->make<HIRExprNodeLoopControl>(v.span(), "", false, nullptr))});
        leaveLoopLabel(label);
        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeLoop>(v.span(), mv$(label), HIRExprNodeP(mCtx.mCrate->pool->make<HIRExprNodeMatch>(v.span(), HIRExprNodeP(mCtx.mCrate->pool->make<HIRExprNodeTuple>(v.span(), ::std::vector<HIRExprNodeP>())), std::move(arms)))));
    }

    virtual void visit(ASTExprNodeMatch& v) override {
        ::std::vector<HIRExprNodeMatch::Arm> arms;

        for (auto& arm : v.arms) {
            HIRExprNodeMatch::Arm newArm{{}, ifletToGuards(arm.guard), lower(arm.mCode)};

            for (const auto& pat : arm.patterns) {
                newArm.patterns.push_back(mCtx.LowerHIRPattern(pat));
            }

            arms.push_back(mv$(newArm));
        }

        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeMatch>(v.span(), lower(v.val), mv$(arms)));
    }

    virtual void visit(ASTExprNodeIf& v) override {
        ::std::vector<HIRExprNodeMatch::Arm> arms;
        // Desugar to a `match`
        for (auto& arm : v.arms) {
            arms.push_back(HIRExprNodeMatch::Arm{makeVec1(HIRPattern()), ifletToGuards(arm.conditions), lower(arm.body)});
        }
        arms.push_back(HIRExprNodeMatch::Arm{makeVec1(HIRPattern()), {}, v.elseNode ? lower(v.elseNode) : HIRExprNodeP(mCtx.mCrate->pool->make<HIRExprNodeTuple>(v.span(), ::std::vector<HIRExprNodeP>()))});

        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeMatch>(v.span(), HIRExprNodeP(mCtx.mCrate->pool->make<HIRExprNodeTuple>(v.span(), ::std::vector<HIRExprNodeP>())), std::move(arms)));
    }

    virtual void visit(ASTExprNodeWildcardPattern& v) override {
        ERROR(v.span(), E0000, "`_` is only valid in expressions on the left-hand side of an assignment");
    }

    virtual void visit(ASTExprNodeInteger& v) override {
        struct H {
            static HIRCoreType getType(Span sp, ::eCoreType ct) {
                switch (ct) {
                    case CORETYPE_ANY:
                        return HIRCoreType::Str;

                    case CORETYPE_I8:
                        return HIRCoreType::I8;
                    case CORETYPE_U8:
                        return HIRCoreType::U8;
                    case CORETYPE_I16:
                        return HIRCoreType::I16;
                    case CORETYPE_U16:
                        return HIRCoreType::U16;
                    case CORETYPE_I32:
                        return HIRCoreType::I32;
                    case CORETYPE_U32:
                        return HIRCoreType::U32;
                    case CORETYPE_I64:
                        return HIRCoreType::I64;
                    case CORETYPE_U64:
                        return HIRCoreType::U64;
                    case CORETYPE_I128:
                        return HIRCoreType::I128;
                    case CORETYPE_U128:
                        return HIRCoreType::U128;

                    case CORETYPE_INT:
                        return HIRCoreType::Isize;
                    case CORETYPE_UINT:
                        return HIRCoreType::Usize;

                    case CORETYPE_CHAR:
                        return HIRCoreType::Char;

                    default:
                        BUG(sp, "Unknown type for integer literal - " << coretypeName(ct));
                }
            }
        };

        if (v.datatype == CORETYPE_F16 || v.datatype == CORETYPE_F32 || v.datatype == CORETYPE_F64) {
            DEBUG("Integer annotated as float, create float node");
            HIRCoreType type;
            switch (v.datatype) {
                case CORETYPE_F16:
                    type = HIRCoreType::F16;
                    break;
                case CORETYPE_F32:
                    type = HIRCoreType::F32;
                    break;
                case CORETYPE_F64:
                    type = HIRCoreType::F64;
                    break;
                default:
                    BUG(v.span(), "Unexpected floating point type");
            }
            mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeLiteral>(v.span(), HIRExprNodeLiteral::Data::make_Float({type, v.mValue.toDouble()})));
            return;
        }
        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeLiteral>(v.span(), HIRExprNodeLiteral::Data::make_Integer({H::getType(v.span(), v.datatype), v.mValue})));
    }

    virtual void visit(ASTExprNodeFloat& v) override {
        HIRCoreType ct;
        switch (v.datatype) {
            case CORETYPE_ANY:
                ct = HIRCoreType::Str;
                break;
            case CORETYPE_F16:
                ct = HIRCoreType::F16;
                break;
            case CORETYPE_F32:
                ct = HIRCoreType::F32;
                break;
            case CORETYPE_F64:
                ct = HIRCoreType::F64;
                break;
            case CORETYPE_F128:
                ct = HIRCoreType::F128;
                break;
            default:
                BUG(v.span(), "Unknown type for float literal - " << coretypeName(v.datatype));
        }
        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeLiteral>(v.span(), HIRExprNodeLiteral::Data::make_Float({ct, v.mValue})));
    }

    virtual void visit(ASTExprNodeBool& v) override {
        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeLiteral>(v.span(), HIRExprNodeLiteral::Data::make_Boolean(v.mValue)));
    }

    virtual void visit(ASTExprNodeString& v) override {
        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeLiteral>(v.span(), HIRExprNodeLiteral::Data::make_String(v.mValue)));
    }

    virtual void visit(ASTExprNodeByteString& v) override {
        ::std::vector<char> dat{v.mValue.begin(), v.mValue.end()};
        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeLiteral>(v.span(), HIRExprNodeLiteral::Data::make_ByteString(mv$(dat))));
    }

    virtual void visit(ASTExprNodeCString& v) override {
        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeLiteral>(v.span(), HIRExprNodeLiteral::Data::make_CString({v.mValue})));
    }

    virtual void visit(ASTExprNodeClosure& v) override {
        HIRExprNodeClosure::argsT args;
        for (const auto& arg : v.mArgs) {
            args.push_back(::std::make_pair(mCtx.LowerHIRPattern(arg.first), mCtx.LowerHIRType(arg.second)));
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
            mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeGenerator>(v.span(), mCtx.LowerHIRType(v.returnType), mCtx.mCrate->types.infer(), mCtx.mCrate->types.infer(), mv$(inner), v.isMove, v.isPinned));
        } else {
            if (v.isPinned) {
                ERROR(v.span(), E0000, "Invalid use of `static` on non-yielding closure");
            }
            mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeClosure>(v.span(), std::move(args), mCtx.LowerHIRType(v.returnType), std::move(inner), v.isMove));
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

        HIRExprNodeStructLiteral::tValues values;
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
                    kind = data.is_Unit() ? EmptyKind::Unit : data.is_Tuple() && data.as_Tuple().mItems.empty() ? EmptyKind::Tuple : EmptyKind::None;
                } else if (binding->hir) {
                    const auto& enm = *binding->hir;
                    if (enm.mData.is_Value()) {
                        kind = EmptyKind::Unit;
                    } else {
                        const auto& var = enm.mData.as_Data().at(binding->idx);
                        if (var.type == mCtx.mCrate->types.unit()) {
                            kind = EmptyKind::Unit;
                        } else {
                            const auto& str = *var.type->as_Path().binding.as_Struct();
                            kind = str.mData.is_Unit() ? EmptyKind::Unit : str.mData.is_Tuple() && str.mData.as_Tuple().empty() ? EmptyKind::Tuple : EmptyKind::None;
                        }
                    }
                }
                if (kind == EmptyKind::Unit) {
                    mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeUnitVariant>(v.span(), mCtx.LowerHIRGenericPath(v.span(), v.mPath, FromASTPathClass::Type), false));
                    return;
                }
                if (kind == EmptyKind::Tuple) {
                    mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeTupleVariant>(v.span(), mCtx.LowerHIRGenericPath(v.span(), v.mPath, FromASTPathClass::Type), false, ::std::vector<HIRExprNodeP>{}));
                    return;
                }
            } else if (const auto* binding = v.mPath.mBindings.type.binding.opt_Struct()) {
                EmptyKind kind = EmptyKind::None;
                if (binding->struct_) {
                    const auto& data = binding->struct_->mData;
                    kind = data.is_Unit() ? EmptyKind::Unit : data.is_Tuple() && data.as_Tuple().ents.empty() ? EmptyKind::Tuple : EmptyKind::None;
                } else if (binding->hir) {
                    const auto& data = binding->hir->mData;
                    kind = data.is_Unit() ? EmptyKind::Unit : data.is_Tuple() && data.as_Tuple().empty() ? EmptyKind::Tuple : EmptyKind::None;
                }
                if (kind == EmptyKind::Unit) {
                    mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeUnitVariant>(v.span(), mCtx.LowerHIRGenericPath(v.span(), v.mPath, FromASTPathClass::Type), true));
                    return;
                }
                if (kind == EmptyKind::Tuple) {
                    mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeTupleVariant>(v.span(), mCtx.LowerHIRGenericPath(v.span(), v.mPath, FromASTPathClass::Type), true, ::std::vector<HIRExprNodeP>{}));
                    return;
                }
            }
        }
        auto ty = mCtx.LowerHIRType(::mkType(*mCtx.mCrate->pool, v.span(), v.mPath));
        if (v.mPath.mBindings.type.binding.is_EnumVar()) {
            ASSERT_BUG(v.span(), TU_TEST1(*ty, Path, .path.mData.is_Generic()), "Enum variant path not GenericPath: " << ty);
            auto data = ty->cloneData();
            auto& gp = data.as_Path().path.mData.as_Generic();
            auto varName = gp.mPath.popComponent();
            auto enumTy = mCtx.mCrate->types.intern(mv$(data));
            ty = mCtx.mCrate->types.path(HIRPath(enumTy, mv$(varName)), {});
        }
        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeStructLiteral>(v.span(), mv$(ty), !v.mPath.mBindings.type.binding.is_EnumVar(), lowerOpt(v.baseValue), mv$(values)));
    }

    virtual void visit(ASTExprNodeStructLiteralPattern& v) override {
        if (v.mPath.mBindings.type.binding.is_Union()) {
            if (v.values.size() != 1) {
                ERROR(v.span(), E0000, "Union constructors can only specify a single field");
            }
        }

        HIRExprNodeStructLiteral::tValues values;
        for (auto& val : v.values) {
            values.push_back(::std::make_pair(val.name, lower(val.value)));
        }
        auto ty = mCtx.LowerHIRType(::mkType(*mCtx.mCrate->pool, v.span(), v.mPath));
        if (v.mPath.mBindings.type.binding.is_EnumVar()) {
            ASSERT_BUG(v.span(), TU_TEST1(*ty, Path, .path.mData.is_Generic()), "Enum variant path not GenericPath: " << ty);
            auto data = ty->cloneData();
            auto& gp = data.as_Path().path.mData.as_Generic();
            auto varName = gp.mPath.popComponent();
            auto enumTy = mCtx.mCrate->types.intern(mv$(data));
            ty = mCtx.mCrate->types.path(HIRPath(enumTy, mv$(varName)), {});
        }
        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeStructLiteral>(v.span(), mv$(ty), !v.mPath.mBindings.type.binding.is_EnumVar(), true, mv$(values)));
    }

    virtual void visit(ASTExprNodeArray& v) override {
        if (v.mSize) {
            mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeArraySized>(
                v.span(),
                lower(v.values.at(0)),
                // TODO: Should this size be a full expression on its own?
                lower(v.mSize)
            ));
        } else {
            ::std::vector<HIRExprNodeP> vals;
            for (auto& val : v.values) {
                vals.push_back(lower(val));
            }
            mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeArrayList>(v.span(), mv$(vals)));
        }
    }

    virtual void visit(ASTExprNodeTuple& v) override {
        ::std::vector<HIRExprNodeP> vals;
        for (auto& val : v.values) {
            vals.push_back(lower(val));
        }
        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeTuple>(v.span(), mv$(vals)));
    }

    virtual void visit(ASTExprNodeNamedValue& v) override {
        if (const auto* e = v.mPath.cls.opt_Local()) {
            TU_MATCH_HDRA( (v.mPath.mBindings.value.binding), {)
            default:
                BUG(v.span(), "Named value was a local, but wasn't bound to a known type - " << v.mPath);
                TU_ARMA(Generic, binding) {
                    mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeConstParam>(v.span(), e->name, binding.index));
                }
                TU_ARMA(Variable, binding) {
                    mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeVariable>(v.span(), e->name, binding.slot));
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
                        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodePathValue>(v.span(), mCtx.LowerHIRPath(v.span(), v.mPath, FromASTPathClass::Value), HIRExprNodePathValue::STRUCT_CONSTR));
                    } else {
                        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeUnitVariant>(v.span(), mCtx.LowerHIRGenericPath(v.span(), v.mPath, FromASTPathClass::Value), true));
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
                            if (ee->at(idx).type == mCtx.mCrate->types.unit()) {
                            }
                            // TODO: Assert that it's not a struct-like
                            else {
                                isTupleConstructor = true;
                            }
                        }
                    }
                    (void)varIdx; // TODO: Save time later by saving this.
                    if (isTupleConstructor) {
                        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodePathValue>(v.span(), mCtx.LowerHIRPath(v.span(), v.mPath, FromASTPathClass::Value), HIRExprNodePathValue::ENUM_VAR_CONSTR));
                    } else {
                        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeUnitVariant>(v.span(), mCtx.LowerHIRGenericPath(v.span(), v.mPath, FromASTPathClass::Value), false));
                    }
                }
                TU_ARMA(Function, e) {
                    mRv.reset(mCtx.mCrate->pool->make<HIRExprNodePathValue>(v.span(), mCtx.LowerHIRPath(v.span(), v.mPath, FromASTPathClass::Value), HIRExprNodePathValue::FUNCTION));
                }
                TU_ARMA(Static, e) {
                    if (e.static_) {
                        if (e.static_->sClass() != ASTStatic::CONST) {
                            mRv.reset(mCtx.mCrate->pool->make<HIRExprNodePathValue>(v.span(), mCtx.LowerHIRPath(v.span(), v.mPath, FromASTPathClass::Value), HIRExprNodePathValue::STATIC));
                        } else {
                            mRv.reset(mCtx.mCrate->pool->make<HIRExprNodePathValue>(v.span(), mCtx.LowerHIRPath(v.span(), v.mPath, FromASTPathClass::Value), HIRExprNodePathValue::CONSTANT));
                        }
                    } else if (e.hir) {
                        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodePathValue>(v.span(), mCtx.LowerHIRPath(v.span(), v.mPath, FromASTPathClass::Value), HIRExprNodePathValue::STATIC));
                    }
                    // HACK: If the HIR pointer is nullptr, then it refers to a `const
                    else {
                        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodePathValue>(v.span(), mCtx.LowerHIRPath(v.span(), v.mPath, FromASTPathClass::Value), HIRExprNodePathValue::CONSTANT));
                    }
                }
                break;
                default:
                    auto p = mCtx.LowerHIRPath(v.span(), v.mPath, FromASTPathClass::Value);
                    ASSERT_BUG(v.span(), !p.mData.is_Generic(), "Unknown binding for PathValue but path is generic - " << v.mPath);
                    mRv.reset(mCtx.mCrate->pool->make<HIRExprNodePathValue>(v.span(), mv$(p), HIRExprNodePathValue::UNKNOWN));
            }
        }
    }

    virtual void visit(ASTExprNodeField& v) override {
        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeField>(v.span(), lower(v.obj), v.mName));
    }

    virtual void visit(ASTExprNodeIndex& v) override {
        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeIndex>(v.span(), lower(v.obj), lower(v.idx)));
    }

    virtual void visit(ASTExprNodeDeref& v) override {
        mRv.reset(mCtx.mCrate->pool->make<HIRExprNodeDeref>(v.span(), lower(v.mValue)));
    }
};

HIRExprPtr AST2HIR::LowerHIRExprNode(const ASTExprNode& e) {
    LowerHIRExprNodeVisitor v(*this);

    const_cast<ASTExprNode*>(&e)->visit(v);

    if (!v.mRv) {
        BUG(e.span(), typeid(e).name() << " - Yielded a nullptr HIR node");
    }

    struct InitialiseResultTypes final: HIRExprVisitorDef {
        explicit InitialiseResultTypes(HIRTypeInterner& types)
            : HIRExprVisitorDef(types)
        {
        }

        void visitNodePtr(HIRExprNodeP& node) override {
            node->resType = typeInterner().infer();
            node->visit(*this);
        }

        void visitType(HIRTypeRef&) override {
        }
    } initialise(mCrate->types);

    initialise.visitNodePtr(v.mRv);

    return HIRExprPtr(mv$(v.mRv));
}
