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

#include <algorithm>
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
    const WireBoard* wb = nullptr;
    HIRSimplePath pathSized;
    HIRSimplePath pathPointeeSized;
    HIRSimplePath pathMetadataSized;
    RcString coreCrate; // lowering-internal working copy; the canonical value lives in Settings
    RcString crateName;
    HIRCrate* crate = nullptr;
    const ASTCrate* astCrate = nullptr;
    ImplTraitSource implTraitSource;

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
    ::std::vector<HIRSimplePath> LowerHIRDefineOpaque(HIRItemPath p, const HIRSimplePath& sourceModule, const ASTAttributeList& attrs);
    HIRFunction LowerHIRFunction(HIRItemPath p, const HIRSimplePath& sourceModule, const ASTAttributeList& attrs, const ASTFunction& f, const HIRTypeData* realSelfType);
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
    return HIRPublicity::newPriv(HIRSimplePath((ap->crate == "" ? crateName : ap->crate), ap->nodes));
}

HIRGenericParams AST2HIR::LowerHIRGenericParams(const ASTGenericParams& gp, bool* selfIsSized) {
    HIRGenericParams rv;

    for (const auto& param : gp.params) {
        switch (param.tag()) {
            case GenericParam::TAG_None: {
                auto& _ = param.as_None();
                (void)_;
                break;
            }
            case GenericParam::TAG_Lifetime: {
                auto& lftDef = param.as_Lifetime();
                (void)lftDef;
                break;
            }
            case GenericParam::TAG_Type: {
                auto& tp = param.as_Type();
                rv.types.push_back({tp.name(), LowerHIRType(tp.getDefault()), true});
                break;
            }
            case GenericParam::TAG_Value: {
                auto& tp = param.as_Value();
                rv.values.push_back(HIRValueParamDef{tp.name().name, LowerHIRType(tp.type()), tp.defaultValue() ? LowerHIRConstGeneric(tp.defaultValue().node()) : HIRConstGeneric::make_Infer({})});
                break;
            }
        }
    }

    for (const auto& bound : gp.bounds) {
        switch (bound.tag()) {
            case ASTGenericBound::TAG_None: {
                auto& e = bound.as_None();
                (void)e;
                break;
            }
            case ASTGenericBound::TAG_Lifetime: {
                auto& e = bound.as_Lifetime();
                (void)e;
                // Lifetimes are erased in HIR
                break;
            }
            case ASTGenericBound::TAG_TypeLifetime: {
                auto& e = bound.as_TypeLifetime();
                (void)e;
                // Lifetimes are erased in HIR
                break;
            }
            case ASTGenericBound::TAG_IsTrait: {
                auto& e = bound.as_IsTrait();
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
                if (boundTraitPath.path.path == pathPointeeSized || boundTraitPath.path.path == pathMetadataSized) {
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
                        rv.bounds.push_back(HIRGenericBound::make_TraitBound({crate->types.path(HIRPath(type, srcTrait.clone(), name, params.clone()), {}), std::move(trait)}));
                    }
                    bound.second.traits.clear();
                }
                break;
            }
            case ASTGenericBound::TAG_MaybeTrait: {
                auto& e = bound.as_MaybeTrait();
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
                if (trait.path == pathSized) {
                    if (paramIdx == 0xFFFF) {
                        assert(selfIsSized);
                        *selfIsSized = false;
                    } else {
                        assert(paramIdx < rv.types.size());
                        rv.types[paramIdx].isSized = false;
                    }
                } else {
                    ERROR(bound.span, E0000, "MaybeTrait on unknown trait " << trait.path);
                }
                break;
            }
            case ASTGenericBound::TAG_NotTrait: {
                auto& e = bound.as_NotTrait();
                (void)e;
                TODO(bound.span, "Negative trait bounds");
                break;
            }
            case ASTGenericBound::TAG_Equality: {
                auto& e = bound.as_Equality();
                rv.bounds.push_back(HIRGenericBound::make_TypeEquality({LowerHIRType(e.type), LowerHIRType(e.replacement)}));
                break;
            }
        }
    }

    return rv;
}

HIRPath AST2HIR::LowerHIRPatternPath(const Span& sp, const ASTPath& path, FromASTPathClass pc) {
    if (const auto* be = path.bindings.type.binding.opt_TypeParameter()) {
        if (be->slot == GENERICSelf) {
            // HACK: Return `<Self>::` (to be expanded later on)
            return HIRPath(crate->types.self(), "");
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
        bindings.push_back(HIRPatternBinding(pb.isMutable, convertBindingType(pb.type), pb.name.name, pb.slot));
    }

    struct H {
        AST2HIR& ctx;
        explicit H(AST2HIR& ctx) : ctx(ctx) {}
        ::std::vector<HIRPattern> lowerhirPatternvec(const ::std::vector<ASTPattern>& subPatterns) {
            ::std::vector<HIRPattern> rv;
            for (const auto& sp : subPatterns) {
                rv.push_back(ctx.LowerHIRPattern(sp));
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
            switch (v.tag()) {
                case ASTPatternValue::TAG_Invalid: {
                    auto& e = v.as_Invalid();
                    (void)e;
                    BUG(sp, "Encountered Invalid value in Pattern");
                    break;
                }
                case ASTPatternValue::TAG_Integer: {
                    auto& e = v.as_Integer();
                    return HIRPattern::Value::make_Integer({getIntType(sp, e.type), e.value});
                }
                case ASTPatternValue::TAG_Float: {
                    auto& e = v.as_Float();
                    return HIRPattern::Value::make_Float({getFloatType(sp, e.type), e.value});
                }
                case ASTPatternValue::TAG_String: {
                    auto& e = v.as_String();
                    return HIRPattern::Value::make_String(e);
                }
                case ASTPatternValue::TAG_ByteString: {
                    auto& e = v.as_ByteString();
                    return HIRPattern::Value::make_ByteString({e.v});
                }
                case ASTPatternValue::TAG_Named: {
                    auto& e = v.as_Named();
                    return HIRPattern::Value::make_Named({ctx.LowerHIRPatternPath(sp, e, FromASTPathClass::Value), nullptr});
                }
            }
            throw "BUGCHECK: Reached end of LowerHIR_Pattern::H::lowerhir_pattern_value";
        }
    };

    switch (pat.data().tag()) {
        case ASTPatternData::TAG_MaybeBind: {
            auto& e = pat.data().as_MaybeBind();
            (void)e;
            BUG(pat.span(), "Encountered MaybeBind pattern");
            break;
        }
        case ASTPatternData::TAG_Macro: {
            auto& e = pat.data().as_Macro();
            (void)e;
            BUG(pat.span(), "Encountered Macro pattern");
            break;
        }
        case ASTPatternData::TAG_Any: {
            auto& e = pat.data().as_Any();
            (void)e;
            return HIRPattern{mv$(bindings), HIRPattern::Data::make_Any({})};
        }
        case ASTPatternData::TAG_Never: {
            auto& e = pat.data().as_Never();
            (void)e;
            return HIRPattern{mv$(bindings), HIRPattern::Data::make_Any({})};
        }
        case ASTPatternData::TAG_Box: {
            auto& e = pat.data().as_Box();
            return HIRPattern{mv$(bindings), HIRPattern::Data::make_Box({box$(LowerHIRPattern(*e.sub))})};
        }
        case ASTPatternData::TAG_Deref: {
            auto& e = pat.data().as_Deref();
            return HIRPattern{mv$(bindings), HIRPattern::Data::make_Deref({HIRPattern::DerefKind::Unknown, nullptr, box$(LowerHIRPattern(*e.sub))})};
        }
        case ASTPatternData::TAG_Ref: {
            auto& e = pat.data().as_Ref();
            return HIRPattern{mv$(bindings), HIRPattern::Data::make_Ref({(e.mut ? HIRBorrowType::Unique : HIRBorrowType::Shared), box$(LowerHIRPattern(*e.sub))})};
        }
        case ASTPatternData::TAG_Tuple: {
            auto& e = pat.data().as_Tuple();
            auto leading = H(*this).lowerhirPatternvec(e.start);
            auto trailing = H(*this).lowerhirPatternvec(e.end);

            if (e.hasWildcard) {
                return HIRPattern(mv$(bindings), HIRPattern::Data::make_SplitTuple({mv$(leading), mv$(trailing)}));
            } else {
                assert(trailing.size() == 0);
                return HIRPattern(mv$(bindings), HIRPattern::Data::make_Tuple({mv$(leading)}));
            }
            break;
        }
        case ASTPatternData::TAG_StructTuple: {
            auto& e = pat.data().as_StructTuple();
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
        case ASTPatternData::TAG_Struct: {
            auto& e = pat.data().as_Struct();
            ::std::vector<::std::pair<RcString, HIRPattern>> subPatterns;
            for (const auto& sp : e.subPatterns) {
                subPatterns.push_back(::std::make_pair(sp.name, LowerHIRPattern(sp.pat)));
            }

            // No sub-patterns, no `..`, and the VALUE binding points to an enum variant
            if (e.subPatterns.empty() /*&& !e.is_exhaustive*/) {
                if (/*const auto* pbp =*/e.path.bindings.value.binding.opt_EnumVar()) {
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
        case ASTPatternData::TAG_Value: {
            auto& e = pat.data().as_Value();
            if (e.end.is_Invalid()) {
                return HIRPattern{mv$(bindings), HIRPattern::Data::make_Value({H(*this).lowerhirPatternValue(pat.span(), e.start)})};
            } else if (e.start.is_Invalid()) {
                return HIRPattern{mv$(bindings), HIRPattern::Data::make_Range({{}, box$(H(*this).lowerhirPatternValue(pat.span(), e.end)), true})};
            } else {
                return HIRPattern{mv$(bindings), HIRPattern::Data::make_Range({box$(H(*this).lowerhirPatternValue(pat.span(), e.start)), box$(H(*this).lowerhirPatternValue(pat.span(), e.end)), true})};
            }
            break;
        }
        case ASTPatternData::TAG_ValueLeftInc: {
            auto& e = pat.data().as_ValueLeftInc();
            if (e.end.is_Invalid()) {
                return HIRPattern{mv$(bindings), HIRPattern::Data::make_Range({box$(H(*this).lowerhirPatternValue(pat.span(), e.start)), {}, false})};
            }
            if (e.start.is_Invalid()) {
                return HIRPattern{mv$(bindings), HIRPattern::Data::make_Range({{}, box$(H(*this).lowerhirPatternValue(pat.span(), e.end)), false})};
            }
            return HIRPattern{mv$(bindings), HIRPattern::Data::make_Range({box$(H(*this).lowerhirPatternValue(pat.span(), e.start)), box$(H(*this).lowerhirPatternValue(pat.span(), e.end)), false})};
        }
        case ASTPatternData::TAG_Slice: {
            auto& e = pat.data().as_Slice();
            ::std::vector<HIRPattern> leading;
            for (const auto& sp : e.subPats) {
                leading.push_back(LowerHIRPattern(sp));
            }
            return HIRPattern{mv$(bindings), HIRPattern::Data::make_Slice({mv$(leading)})};
        }
        case ASTPatternData::TAG_SplitSlice: {
            auto& e = pat.data().as_SplitSlice();
            ::std::vector<HIRPattern> leading;
            for (const auto& sp : e.leading) {
                leading.push_back(LowerHIRPattern(sp));
            }

            ::std::vector<HIRPattern> trailing;
            for (const auto& sp : e.trailing) {
                trailing.push_back(LowerHIRPattern(sp));
            }

            if (e.extraRest) {
                ERROR(pat.span(), E0000, "A slice pattern takes at most one `..`");
            }

            auto extraBind = e.extraBind.isValid() ? HIRPatternBinding(false, convertBindingType(e.extraBind.type), e.extraBind.name.name, e.extraBind.slot) : HIRPatternBinding();

            return HIRPattern{mv$(bindings), HIRPattern::Data::make_SplitSlice({mv$(leading), mv$(extraBind), mv$(trailing)})};
        }
        case ASTPatternData::TAG_Or: {
            auto& e = pat.data().as_Or();
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
            ASSERT_BUG(sp, !path.bindings.value.is_Unbound(), "Encountered unbound value path - " << path);
            ap = &path.bindings.value.path;
            break;
        case FromASTPathClass::Type:
            ASSERT_BUG(sp, !path.bindings.type.is_Unbound(), "Encountered unbound type path - " << path);
            ap = &path.bindings.type.path;
            break;
        case FromASTPathClass::Macro:
            ASSERT_BUG(sp, !path.bindings.macro.is_Unbound(), "Encountered unbound macro path - " << path);
            ap = &path.bindings.macro.path;
            break;
    }
    assert(ap);
    return HIRSimplePath((ap->crate == "" ? crateName : ap->crate), ap->nodes);
}

HIRPathParams AST2HIR::LowerHIRPathParams(const Span& sp, const ASTPathParams& srcParams, bool allowAssoc) {
    HIRPathParams params;

    size_t numLft = 0;
    size_t numTy = 0;
    size_t numVal = 0;

    for (const auto& param : srcParams.entries) {
        switch (param.tag()) {
            case ASTPathParamEnt::TAG_Null: {
                auto& ty = param.as_Null();
                (void)ty;
                break;
            }
            case ASTPathParamEnt::TAG_Lifetime: {
                auto& lft = param.as_Lifetime();
                (void)lft;
                numLft++;
                break;
            }
            case ASTPathParamEnt::TAG_Type: {
                auto& ty = param.as_Type();
                (void)ty;
                numTy++;
                break;
            }
            case ASTPathParamEnt::TAG_Value: {
                auto& iv = param.as_Value();
                (void)iv;
                numVal++;
                break;
            }
            case ASTPathParamEnt::TAG_AssociatedTyEqual: {
                auto& ty = param.as_AssociatedTyEqual();
                (void)ty;
                break;
            }
            case ASTPathParamEnt::TAG_AssociatedValueEqual: {
                auto& ty = param.as_AssociatedValueEqual();
                (void)ty;
                break;
            }
            case ASTPathParamEnt::TAG_AssociatedTyBound: {
                auto& ty = param.as_AssociatedTyBound();
                (void)ty;
                break;
            }
        }
    }

    params.types.reserveInit(numTy);
    params.values.reserveInit(numVal);
    for (const auto& param : srcParams.entries) {
        switch (param.tag()) {
            case ASTPathParamEnt::TAG_Null: {
                auto& ty = param.as_Null();
                (void)ty;
                break;
            }
            case ASTPathParamEnt::TAG_Lifetime: {
                auto& lft = param.as_Lifetime();
                (void)lft;
                break;
            }
            case ASTPathParamEnt::TAG_Type: {
                auto& ty = param.as_Type();
                params.types.push_back(LowerHIRType(ty));
                break;
            }
            case ASTPathParamEnt::TAG_Value: {
                auto& iv = param.as_Value();
                ASSERT_BUG(sp, iv, "Value parameter with null node");
                params.values.push_back(LowerHIRConstGeneric(*iv));
                break;
            }
            case ASTPathParamEnt::TAG_AssociatedTyEqual: {
                auto& ty = param.as_AssociatedTyEqual();
                (void)ty;
                if (!allowAssoc) {
                    BUG(sp, "Encountered path parameters with associated type bounds where they are not allowed");
                }
                break;
            }
            case ASTPathParamEnt::TAG_AssociatedValueEqual: {
                auto& ty = param.as_AssociatedValueEqual();
                (void)ty;
                if (!allowAssoc) {
                    BUG(sp, "Encountered path parameters with associated type bounds where they are not allowed");
                }
                // TODO: `Trait<K = 0>` constrains an associated const. The
                // equality is parsed and dropped: nothing checks it yet, so a
                // program that violates it is accepted.
                break;
            }
            case ASTPathParamEnt::TAG_AssociatedTyBound: {
                auto& ty = param.as_AssociatedTyBound();
                (void)ty;
                if (!allowAssoc) {
                    BUG(sp, "Encountered path parameters with associated type bounds where they are not allowed");
                }
                break;
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
        if (e->path.isTrivial()) {
            const auto& b = e->path.bindings.value.binding;
            ASSERT_BUG(sp, b.is_Generic(), "Trivial path not type parameter - " << e->path << " - " << b.tagStr());
            const auto& param = b.as_Generic();
            return HIRGenericRef(e->path.asTrivial(), param.index);
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
            } else if (!e->type->data.is_Path()) {
            } else {
                // HACK: `Self` replacement
                ASSERT_BUG(sp, pc == FromASTPathClass::Type, "`Self` used in value context");
                return LowerHIRGenericPath(sp, *e->type->data.as_Path(), pc, false);
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

        AST2HIR& ctx;
        explicit H(AST2HIR& ctx) : ctx(ctx) {}
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
            auto selfTy = ctx.crate->types.self();
            auto cb = MonomorphStatePtr(ctx.crate->types, selfTy, &path.params, nullptr);
            for (const auto& st : trait.allParentTraits) {
                // NOTE: st.m_trait_ptr isn't populated yet
                const auto& t = ctx.crate->getTraitByPath(sp, st.path.path);

                if (hasItem(t, name, ns)) {
                    // Monomorphse into outer scope, then run the outer monomorph
                    auto p = cb.monomorphGenericpath(sp, st.path, /*allow_infer=*/true);
                    return ms.monomorphGenericpath(sp, p, /*allow_infer=*/true);
                }
            }
            return HIRGenericPath();
        }

        HIRGenericPath findSourceTraitAst(const Span& sp, const HIRGenericPath& path, const ASTTrait& trait, const RcString& name, Namespace ns, const Monomorphiser& ms) {
            if (hasItem(trait, name, ns)) {
                return ms.monomorphGenericpath(sp, path, /*allow_infer=*/true);
            }

            auto selfTy = ctx.crate->types.self();
            auto cb = MonomorphStatePtr(ctx.crate->types, selfTy, &path.params, nullptr);
            for (const auto& st : trait.supertraits()) {
                auto b = ctx.LowerHIRTraitPath(sp, *st.ent.path, st.ent.hrbs, true, st.ent.constness);
                ASSERT_BUG(sp, st.ent.path->bindings.type.binding.is_Trait(), "Not a trait: " << *st.ent.path);
                auto rv = findSourceTrait(sp, b.path, st.ent.path->bindings.type.binding.as_Trait(), name, ns, cb);
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
                        auto p = ms.monomorphGenericpath(sp, subTrait.path);
                        const auto& t = ctx.crate->getTraitByPath(sp, p.path);
                        auto selfTy = ctx.crate->types.self();
                        auto rv = findSourceTraitHir(sp, p, t, name, ns, MonomorphStatePtr(ctx.crate->types, selfTy, &p.params, nullptr));
                        if (rv != HIRGenericPath()) {
                            return rv;
                        }
                    }
                    return HIRGenericPath();
                } else if (pbe.trait_) {
                    auto selfTy = ctx.crate->types.self();
                    auto cb = MonomorphStatePtr(ctx.crate->types, selfTy, &path.params, nullptr);
                    for (const auto& st : pbe.trait_->traits) {
                        auto b = ctx.LowerHIRTraitPath(sp, *st.ent.path, st.ent.hrbs, true, st.ent.constness);
                        auto rv = findSourceTrait(sp, b.path, st.ent.path->bindings.type.binding, name, ns, cb);
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
            auto args = ctx.LowerHIRPathParams(sp, pn.args(), false);
            if (args.hasParams()) {
                TODO(sp, "Handle ATYs with args");
            }
            return std::make_pair(pn.name(), std::move(args));
        }
    };

    for (const auto& e : path.nodes().back().args().entries) {
        switch (e.tag()) {
            case ASTPathParamEnt::TAG_Null: {
                auto& _ = e.as_Null();
                (void)_;
                break;
            }
            case ASTPathParamEnt::TAG_Lifetime: {
                auto& _ = e.as_Lifetime();
                (void)_;
                break;
            }
            case ASTPathParamEnt::TAG_Type: {
                auto& _ = e.as_Type();
                (void)_;
                break;
            }
            case ASTPathParamEnt::TAG_Value: {
                auto& _ = e.as_Value();
                (void)_;
                break;
            }
            case ASTPathParamEnt::TAG_AssociatedTyEqual: {
                auto& assoc = e.as_AssociatedTyEqual();
                if (assoc.first.args().isRtn) {
                    ERROR(sp, E0000, "Return-type notation does not support equality constraints");
                }
                auto nameArgs = H(*this).getAtyNode(sp, assoc.first);
                auto srcTrait = H(*this).findSourceTrait(sp, rv.path, path.bindings.type.binding, nameArgs.first, H::Namespace::Type, MonomorphiserNop(crate->types));
                DEBUG("src_trait = " << srcTrait << " for " << assoc.first);
                rv.typeBounds.insert(::std::make_pair(nameArgs.first, HIRTraitPath::AtyEqual{std::move(srcTrait), std::move(nameArgs.second), LowerHIRType(assoc.second)}));
                break;
            }
            case ASTPathParamEnt::TAG_AssociatedTyBound: {
                auto& assoc = e.as_AssociatedTyBound();
                if (!ignoreBounds) {
                    ERROR(sp, E0000, "Associated type trait bounds not allowed here - " << path);
                } else {
                    auto nameArgs = H(*this).getAtyNode(sp, assoc.first);
                    const auto sourceName = nameArgs.first;
                    const auto ns = assoc.first.args().isRtn ? H::Namespace::Function : H::Namespace::Type;
                    auto srcTrait = H(*this).findSourceTrait(sp, rv.path, path.bindings.type.binding, sourceName, ns, MonomorphiserNop(crate->types));
                    if (assoc.first.args().isRtn) {
                        nameArgs.first = RcString::newInterned(FMT(ATY_PREFIX_ERASED << sourceName << "_0"));
                    }
                    DEBUG("src_trait = " << srcTrait << " for " << assoc.first);
                    //if(src_trait == ::HIR::GenericPath())
                    auto it = rv.traitBounds.insert(std::make_pair(nameArgs.first, HIRTraitPath::AtyBound{std::move(srcTrait), std::move(nameArgs.second), {}}));
                    for (const auto& trait : assoc.second) {
                        it.first->second.traits.push_back(LowerHIRTraitPath(sp, *trait.path, trait.hrbs, /*ignore_bounds*/ true, trait.constness));
                    }
                }
                break;
            }
        }
    }

    return rv;
}

HIRPath AST2HIR::LowerHIRPath(const Span& sp, const ASTPath& path, FromASTPathClass pc) {
    switch (path.cls.tag()) {
        case ASTPathClass::TAG_Invalid: {
            auto& e = path.cls.as_Invalid();
            (void)e;
            BUG(sp, "BUG: Encountered Invalid path in LowerHIR_Path");
            break;
        }
        case ASTPathClass::TAG_Local: {
            auto& e = path.cls.as_Local();
            (void)e;
            TODO(sp, "What to do with Path::Class::Local in LowerHIR_Path - " << path);
            break;
        }
        case ASTPathClass::TAG_Relative: {
            auto& e = path.cls.as_Relative();
            (void)e;
            BUG(sp, "Encountered `Relative` path in LowerHIR_Path - " << path);
            break;
        }
        case ASTPathClass::TAG_Self: {
            auto& e = path.cls.as_Self();
            (void)e;
            BUG(sp, "Encountered `Self` path in LowerHIR_Path - " << path);
            break;
        }
        case ASTPathClass::TAG_Super: {
            auto& e = path.cls.as_Super();
            (void)e;
            BUG(sp, "Encountered `Super` path in LowerHIR_Path - " << path);
            break;
        }
        case ASTPathClass::TAG_Absolute: {
            auto& e = path.cls.as_Absolute();
            (void)e;
            return HIRPath(LowerHIRGenericPath(sp, path, pc));
        }
        case ASTPathClass::TAG_UFCS: {
            auto& e = path.cls.as_UFCS();
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
            break;
        }
    }
    throw "BUGCHECK: Reached end of LowerHIR_Path";
}

namespace {
    class TraitObjectLowering {
        AST2HIR& ctx_;
        const Span& span_;
        HIRTypeData::Data_TraitObject& out;
        ::std::unordered_set<const void*> activeAliases;

        bool hasPrincipal() const {
            return !out.trait.path.path.components().empty();
        }

        void addTrait(HIRTraitPath trait, bool isMarker) {
            if (isMarker) {
                if (!trait.typeBounds.empty() || !trait.traitBounds.empty()) {
                    ERROR(span_, E0000, "Associated type bounds on auto trait " << trait.path);
                }
                out.markers.push_back(mv$(trait.path));
                return;
            }

            if (hasPrincipal()) {
                ERROR(span_, E0000, "Multiple data traits in trait object: " << out.trait.path << " and " << trait.path);
            }
            out.trait = mv$(trait);
        }

        void applyAliasBounds(HIRTraitPath& aliasPath, bool hadPrincipal) {
            const bool addedPrincipal = !hadPrincipal && hasPrincipal();
            if ((!aliasPath.typeBounds.empty() || !aliasPath.traitBounds.empty()) && !addedPrincipal) {
                ERROR(span_, E0000, "Associated type bounds on trait alias without a data trait: " << aliasPath.path);
            }
            if (addedPrincipal) {
                for (auto& bound : aliasPath.typeBounds) {
                    out.trait.typeBounds.insert(::std::make_pair(bound.first, mv$(bound.second)));
                }
                for (auto& bound : aliasPath.traitBounds) {
                    out.trait.traitBounds.insert(::std::make_pair(bound.first, mv$(bound.second)));
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
                ERROR(span_, E0000, "Recursive trait alias in trait object: " << path);
            }
            return ActiveAlias{activeAliases, key};
        }

        void addAstPath(HIRTraitPath path, const ASTPathBindingType& binding) {
            if (const auto* trait = binding.opt_Trait()) {
                ASSERT_BUG(span_, trait->trait_ || trait->hir, "Null trait binding for " << path.path);
                addTrait(mv$(path), trait->trait_ ? trait->trait_->isMarker() : trait->hir->isMarker);
            } else if (const auto* alias = binding.opt_TraitAlias()) {
                expandAstAlias(mv$(path), *alias);
            } else {
                BUG(span_, "Not a trait or trait alias: " << path.path << " (" << binding.tagStr() << ")");
            }
        }

        void addHirPath(HIRTraitPath path) {
            const auto& item = ctx_.crate->getTypeitemByPath(span_, path.path.path);
            if (const auto* trait = item.opt_Trait()) {
                addTrait(mv$(path), trait->isMarker);
            } else if (const auto* alias = item.opt_TraitAlias()) {
                expandHirAlias(mv$(path), *alias);
            } else {
                BUG(span_, "Trait alias expanded to non-trait path " << path.path << " (" << item.tagStr() << ")");
            }
        }

        void expandAstAlias(HIRTraitPath aliasPath, const ASTPathBindingType::Data_TraitAlias& binding) {
            const void* key = binding.trait_ ? static_cast<const void*>(binding.trait_) : static_cast<const void*>(binding.hir);
            ASSERT_BUG(span_, key, "Null trait alias binding for " << aliasPath.path);
            auto active = enterAlias(key, aliasPath.path);
            const bool hadPrincipal = hasPrincipal();

            if (binding.trait_) {
                bool traitRequiresSized = false;
                auto paramsDef = ctx_.LowerHIRGenericParams(binding.trait_->params, &traitRequiresSized);
                auto params = ConvertHIRCompleteAliasParams(ctx_.crate->types, span_, paramsDef, aliasPath.path, false);
                auto monomorph = MonomorphStatePtr(ctx_.crate->types, nullptr, &params, nullptr);
                for (const auto& bound : binding.trait_->traits) {
                    auto trait = ctx_.LowerHIRTraitPath(bound.sp, *bound.ent.path, bound.ent.hrbs, false, bound.ent.constness);
                    addAstPath(monomorph.monomorphTraitpath(span_, trait, false), bound.ent.path->bindings.type.binding);
                }
            } else {
                ASSERT_BUG(span_, binding.hir, "Null trait alias binding for " << aliasPath.path);
                expandHirAliasContents(aliasPath, *binding.hir);
            }

            applyAliasBounds(aliasPath, hadPrincipal);
        }

        void expandHirAliasContents(const HIRTraitPath& aliasPath, const HIRTraitAlias& alias) {
            auto params = ConvertHIRCompleteAliasParams(ctx_.crate->types, span_, alias.params, aliasPath.path, false);
            auto monomorph = MonomorphStatePtr(ctx_.crate->types, nullptr, &params, nullptr);
            for (const auto& bound : alias.traits) {
                auto trait = bound.clone();
                addHirPath(monomorph.monomorphTraitpath(span_, trait, false));
            }
        }

        void expandHirAlias(HIRTraitPath aliasPath, const HIRTraitAlias& alias) {
            auto active = enterAlias(&alias, aliasPath.path);
            const bool hadPrincipal = hasPrincipal();
            expandHirAliasContents(aliasPath, alias);
            applyAliasBounds(aliasPath, hadPrincipal);
        }

    public:
        TraitObjectLowering(AST2HIR& ctx, const Span& span, HIRTypeData::Data_TraitObject& out)
            : ctx_(ctx)
            , span_(span)
            , out(out)
        {
        }

        void add(const ::TypeTraitPath& bound) {
            auto path = ctx_.LowerHIRTraitPath(span_, *bound.path, bound.hrbs, false, bound.constness);
            addAstPath(mv$(path), bound.path->bindings.type.binding);
        }
    };
}

HIRTypeRef AST2HIR::LowerHIRType(::ASTType* ty) {
    switch (ty->data.tag()) {
        case TypeData::TAG_None: {
            auto& e = ty->data.as_None();
            (void)e;
            BUG(ty->span(), "TypeData::None");
            break;
        }
        case TypeData::TAG_Bang: {
            auto& e = ty->data.as_Bang();
            (void)e;
            return crate->types.diverge();
        }
        case TypeData::TAG_Any: {
            auto& e = ty->data.as_Any();
            (void)e;
            return crate->types.infer();
        }
        case TypeData::TAG_Unit: {
            auto& e = ty->data.as_Unit();
            (void)e;
            return crate->types.unit();
        }
        case TypeData::TAG_Macro: {
            auto& e = ty->data.as_Macro();
            (void)e;
            BUG(ty->span(), "TypeData::Macro");
            break;
        }
        case TypeData::TAG_Primitive: {
            auto& e = ty->data.as_Primitive();
            switch (e.coreType) {
                case CORETYPE_BOOL:
                    return crate->types.primitive(HIRCoreType::Bool);
                case CORETYPE_CHAR:
                    return crate->types.primitive(HIRCoreType::Char);
                case CORETYPE_STR:
                    return crate->types.primitive(HIRCoreType::Str);
                case CORETYPE_F16:
                    return crate->types.primitive(HIRCoreType::F16);
                case CORETYPE_F32:
                    return crate->types.primitive(HIRCoreType::F32);
                case CORETYPE_F64:
                    return crate->types.primitive(HIRCoreType::F64);
                case CORETYPE_F128:
                    return crate->types.primitive(HIRCoreType::F128);

                case CORETYPE_I8:
                    return crate->types.primitive(HIRCoreType::I8);
                case CORETYPE_U8:
                    return crate->types.primitive(HIRCoreType::U8);
                case CORETYPE_I16:
                    return crate->types.primitive(HIRCoreType::I16);
                case CORETYPE_U16:
                    return crate->types.primitive(HIRCoreType::U16);
                case CORETYPE_I32:
                    return crate->types.primitive(HIRCoreType::I32);
                case CORETYPE_U32:
                    return crate->types.primitive(HIRCoreType::U32);
                case CORETYPE_I64:
                    return crate->types.primitive(HIRCoreType::I64);
                case CORETYPE_U64:
                    return crate->types.primitive(HIRCoreType::U64);

                case CORETYPE_I128:
                    return crate->types.primitive(HIRCoreType::I128);
                case CORETYPE_U128:
                    return crate->types.primitive(HIRCoreType::U128);

                case CORETYPE_INT:
                    return crate->types.primitive(HIRCoreType::Isize);
                case CORETYPE_UINT:
                    return crate->types.primitive(HIRCoreType::Usize);
                case CORETYPE_ANY:
                    TODO(ty->span(), "TypeData::Primitive - CORETYPE_ANY");
                case CORETYPE_INVAL:
                    BUG(ty->span(), "TypeData::Primitive - CORETYPE_INVAL");
            }
            break;
        }
        case TypeData::TAG_Tuple: {
            auto& e = ty->data.as_Tuple();
            HIRTypeData::Data_Tuple v;
            for (const auto& st : e.innerTypes) {
                v.push_back(LowerHIRType(st));
            }
            return crate->types.tuple(mv$(v));
        }
        case TypeData::TAG_Borrow: {
            auto& e = ty->data.as_Borrow();
            auto cl = (e.isMut ? HIRBorrowType::Unique : HIRBorrowType::Shared);
            return crate->types.borrow(cl, LowerHIRType(e.inner));
        }
        case TypeData::TAG_Pointer: {
            auto& e = ty->data.as_Pointer();
            auto cl = (e.isMut ? HIRBorrowType::Unique : HIRBorrowType::Shared);
            return crate->types.pointer(cl, LowerHIRType(e.inner));
        }
        case TypeData::TAG_Array: {
            auto& e = ty->data.as_Array();
            auto inner = LowerHIRType(e.inner);
            if (e.size) {
                // If the size expression is an unannotated or usize integer literal, don't bother converting the expression
                if (const auto* ptr = cast<const ASTExprNodeInteger>(&*e.size)) {
                    if (ptr->datatype == CORETYPE_UINT || ptr->datatype == CORETYPE_ANY) {
                        // TODO: Chage the HIR format to support very large arrays
                        if (ptr->value >= U128(UINT64_MAX)) {
                            ERROR(ty->span(), E0000, "Array size out of bounds - 0x" << ::std::hex << ptr->value << " > 0x" << UINT64_MAX << " in " << ::std::dec << ty);
                        }
                        return crate->types.array(inner, ptr->value.truncateU64());
                    }
                }
                if (const auto* ptr = cast<const ASTExprNodeNamedValue>(&*e.size)) {
                    if (ptr->path.isTrivial()) {
                        auto gr = HIRGenericRef(ptr->path.asTrivial(), ptr->path.bindings.value.binding.as_Generic().index);
                        return crate->types.array(inner, HIRConstGeneric(mv$(gr)));
                    }
                }

                // `[T; _]` is an inferred length, the same placeholder the
                // expression form uses.
                if (cast<const ASTExprNodeWildcardPattern>(&*e.size)) {
                    return crate->types.array(inner, HIRConstGeneric::make_Infer({}));
                }
                return crate->types.array(inner, HIRConstGeneric::make_Unevaluated(std::make_unique<HIRConstGenericUnevaluated>(LowerHIRExpr(e.size))));
            } else {
                return crate->types.array(inner, HIRConstGeneric::make_Infer({}));
            }
            break;
        }
        case TypeData::TAG_Slice: {
            auto& e = ty->data.as_Slice();
            auto inner = LowerHIRType(e.inner);
            return crate->types.slice(inner);
        }
        case TypeData::TAG_Pattern: {
            auto& e = ty->data.as_Pattern();
            auto lowerValue = [&](const ASTPattern::Value& value, const Span& sp) -> HIRConstGeneric {
                switch (value.tag()) {
                    case ASTPatternValue::TAG_Integer: {
                        auto& v = value.as_Integer();
                        ASTExprNodeInteger node(v.value, v.type);
                        node.setSpan(sp);
                        return LowerHIRConstGeneric(node);
                    }
                    case ASTPatternValue::TAG_Named: {
                        auto& v = value.as_Named();
                        ASTExprNodeNamedValue node(v);
                        node.setSpan(sp);
                        return LowerHIRConstGeneric(node);
                    }
                    case ASTPatternValue::TAG_Invalid: {
                        auto& v = value.as_Invalid();
                        (void)v;
                        BUG(sp, "invalid pattern endpoint");
                        break;
                    }
                    case ASTPatternValue::TAG_Float: {
                        auto& v = value.as_Float();
                        (void)v;
                        ERROR(sp, E0000, "float pattern types are not supported");
                        break;
                    }
                    case ASTPatternValue::TAG_String: {
                        auto& v = value.as_String();
                        (void)v;
                        ERROR(sp, E0000, "string pattern types are not supported");
                        break;
                    }
                    case ASTPatternValue::TAG_ByteString: {
                        auto& v = value.as_ByteString();
                        (void)v;
                        ERROR(sp, E0000, "byte-string pattern types are not supported");
                        break;
                    }
                }
                throw "";
            };

            HIRTypePattern pattern;
            std::function<void(const ASTPattern&)> lowerPattern = [&](const ASTPattern& pat) {
                switch (pat.data().tag()) {
                    case ASTPatternData::TAG_Value: {
                        auto& range = pat.data().as_Value();
                        if (range.end.is_Invalid()) ERROR(pat.span(), E0000, "pattern types require a range pattern");
                        HIRTypePatternRange out{!range.start.is_Invalid(), {}, !range.end.is_Invalid(), {}, true};
                        if (out.hasStart) out.start = lowerValue(range.start, pat.span());
                        if (out.hasEnd) out.end = lowerValue(range.end, pat.span());
                        pattern.alternatives.push_back(mv$(out));
                        break;
                    }
                    case ASTPatternData::TAG_ValueLeftInc: {
                        auto& range = pat.data().as_ValueLeftInc();
                        HIRTypePatternRange out{!range.start.is_Invalid(), {}, !range.end.is_Invalid(), {}, false};
                        if (out.hasStart) out.start = lowerValue(range.start, pat.span());
                        if (out.hasEnd) out.end = lowerValue(range.end, pat.span());
                        pattern.alternatives.push_back(mv$(out));
                        break;
                    }
                    case ASTPatternData::TAG_Or: {
                        auto& alternatives = pat.data().as_Or();
                        for (const auto& alternative : alternatives) lowerPattern(alternative);
                        break;
                    }
default:
                    ERROR(pat.span(), E0000, "pattern not supported in pattern types");
                }
            };
            lowerPattern(*e.pattern);
            return crate->types.intern(HIRTypeData::make_Pattern({LowerHIRType(e.inner), mv$(pattern)}));
        }
        case TypeData::TAG_Path: {
            auto& e = ty->data.as_Path();
            if (const auto* l = e->cls.opt_Local()) {
                unsigned int slot;
                // NOTE: TypeParameter is unused
                if (const auto* p = e->bindings.type.binding.opt_TypeParameter()) {
                    slot = p->slot;
                } else {
                    BUG(ty->span(), "Unbound local encountered in " << *e);
                }
                return crate->types.generic(l->name, slot);
            } else if (e->bindings.type.path.crate == CRATE_BUILTINS) {
                return LowerHIRType(mkType(*ty->pool, ty->span(), coretypeFromstring(e->bindings.type.path.nodes.back().c_str())));
            } else {
                return crate->types.path(LowerHIRPath(ty->span(), *e, FromASTPathClass::Type), {});
            }
            break;
        }
        case TypeData::TAG_TraitObject: {
            auto& e = ty->data.as_TraitObject();
            HIRTypeData::Data_TraitObject v;
            TraitObjectLowering lowering(*this, ty->span(), v);
            for (const auto& t : e.traits) {
                DEBUG("t = " << *t.path);
                lowering.add(t);
            }
            // Sort markers so downstream can compare properly
            ::std::sort(v.markers.begin(), v.markers.end());
            v.markers.erase(::std::unique(v.markers.begin(), v.markers.end()), v.markers.end());
            return crate->types.intern(HIRTypeData::make_TraitObject(mv$(v)));
        }
        case TypeData::TAG_ErasedType: {
            auto& e = ty->data.as_ErasedType();
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
                if (tp.path.path == pathSized) {
                    isSized = false;
                } else {
                    TODO(ty->span(), "Optional trait (not Sized) - " << ty);
                }
            }
            TypeDataErasedTypeInner inner;
            if (implTraitSource.path) {
                if (implTraitSource.paramsInner && implTraitSource.paramsInner->isGeneric()) {
                    TODO(ty->span(), "Handle multi-layered generic erased type (used in a GAT)");
                }
                inner = TypeDataErasedTypeInner(TypeDataErasedTypeInner::Data_Alias{implTraitSource.paramsOuter->makeNopParams(crate->types, 0), std::make_shared<HIRTypeDataErasedTypeAliasInner>(*implTraitSource.path, *implTraitSource.paramsOuter)});
            } else {
                inner = TypeDataErasedTypeInner::Data_Fcn{HIRPath(HIRSimplePath()), 0}; // Populated in bind, could be populated now?
            }
            return crate->types.intern(HIRTypeData::make_ErasedType({isSized, mv$(traits), mv$(inner), e->use ? LowerHIRPathParams(ty->span(), *e->use, false) : HIRPathParams(), e->use ? HIRTypeDataErasedType::Use::Present : (e->isEdition2024OrLater ? HIRTypeDataErasedType::Use::Omitted2024 : HIRTypeDataErasedType::Use::OmittedOld)}));
        }
        case TypeData::TAG_Function: {
            auto& e = ty->data.as_Function();
            ::std::vector<HIRTypeRef> args;
            for (const auto& arg : e.info.argTypes) {
                args.push_back(LowerHIRType(arg));
            }
            HIRTypeDataFunctionPointer f{e.info.isUnsafe, e.info.isVariadic, RcString::newInterned(e.info.abi), LowerHIRType(e.info.rettype), mv$(args)};
            if (f.abi == "") {
                f.abi = RcString::newInterned(ABI_RUST);
            }
            return crate->types.function(mv$(f));
        }
        case TypeData::TAG_Generic: {
            auto& e = ty->data.as_Generic();
            assert(e.index < 0x10000);
            return crate->types.generic(e.name, e.index);
        }
    }
    throw "BUGCHECK: Reached end of LowerHIR_Type";
}

HIRTypeAlias AST2HIR::LowerHIRTypeAlias(const HIRItemPath& p, const ASTTypeAlias& ta) {
    assert(!implTraitSource.path);
    auto params = LowerHIRGenericParams(ta.params(), nullptr);
    implTraitSource = ImplTraitSource(&p, &params);
    auto ty = LowerHIRType(ta.type());
    //}
    implTraitSource = ImplTraitSource();
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
        auto type = LowerHIRType(field.type);
        ::std::unique_ptr<HIRGenericPath> fieldDefault;
        if (field.defaultValue) {
            // NOTE: I'd love to have this be a `Constant`, but that would require duplicating the type and the params
            // meh. Lazy option is to just duplicate
            auto name = RcString::newInterned(FMT(path.getName() << "#default_" << field.name));
            outMod.valueItems.insert(std::make_pair(name, crate->pool->make<HIRVisEnt<HIRValueItem>>(HIRVisEnt<HIRValueItem>{HIRPublicity::newGlobal(), HIRValueItem(HIRConstant(params.clone(), type, LowerHIRExpr(field.defaultValue)))})));
            fieldDefault = std::make_unique<HIRGenericPath>((*path.parent + name).getSimplePath(), params.makeNopParams(crate->types, 0));
        }
        fields.push_back(HIRStructField{field.name, LowerHIRVis(getParentModule(path), field.vis), std::move(type), std::move(fieldDefault)});
    }
    return fields;
}

namespace {
    /// Record which of the item's own type parameters a type mentions.
    ///
    /// Two shapes hide a parameter from this walk: `Self`, which names the item
    /// with all of its parameters, and a const argument that is still an
    /// unevaluated expression. Both set `opaque`, and the caller then makes no
    /// claim about that item.
    void collectUsedTypeParams(HIRTypeInterner& types, const Span& sp, const HIRTypeData* ty, ::std::set<unsigned>& used, bool& opaque) {
        cloneTyWith(types, sp, ty, [&](const HIRTypeData* tpl, HIRTypeRef&) {
            if (const auto* ge = tpl->opt_Generic()) {
                if (ge->isSelf()) {
                    opaque = true;
                } else if (ge->group() == GENERICImpl) {
                    used.insert(ge->idx());
                }
            }
            if (const auto* ae = tpl->opt_Array()) {
                if (ae->size.is_Unevaluated() && ae->size.as_Unevaluated().is_Unevaluated()) {
                    opaque = true;
                }
            }
            if (const auto* pe = tpl->opt_Path()) {
                if (!pe->path.data.is_Generic()) {
                    // A projection can name the parameter through its `Self`.
                    opaque = true;
                }
            }
            return false;
        });
    }

    /// A parameter named by a bound is determined by whatever satisfies that
    /// bound (`struct S<K, I: Iterator<Item = K>>(I)` constrains `K`), so the
    /// bounds count as uses too.
    void collectUsedTypeParamsInBounds(HIRTypeInterner& types, const Span& sp, const HIRGenericParams& params, ::std::set<unsigned>& used, bool& opaque) {
        for (const auto& bound : params.bounds) {
            switch (bound.tag()) {
                case HIRGenericBound::TAG_TraitBound: {
                    auto& be = bound.as_TraitBound();
                    collectUsedTypeParams(types, sp, be.type, used, opaque);
                    for (const auto& ty : be.trait.path.params.types) {
                        collectUsedTypeParams(types, sp, ty, used, opaque);
                    }
                    for (const auto& assoc : be.trait.typeBounds) {
                        collectUsedTypeParams(types, sp, assoc.second.type, used, opaque);
                    }
                    break;
                }
                case HIRGenericBound::TAG_TypeEquality: {
                    auto& be = bound.as_TypeEquality();
                    collectUsedTypeParams(types, sp, be.type, used, opaque);
                    collectUsedTypeParams(types, sp, be.otherType, used, opaque);
                    break;
                }
            }
        }
    }

    /// A type parameter that no field mentions cannot be inferred at a use
    /// site, so rustc rejects the definition and points at `PhantomData`.
    void checkTypeParamsUsed(const Span& sp, const HIRGenericParams& params, const ::std::set<unsigned>& used, const char* what) {
        for (size_t i = 0; i < params.types.size(); i++) {
            if (used.count(static_cast<unsigned>(i))) {
                continue;
            }
            ERROR(sp, E0000, "parameter `" << params.types[i].name << "` is never used in this " << what << " - consider a `PhantomData` field");
        }
    }
}


HIRStruct AST2HIR::LowerHIRStruct(const Span& sp, HIRItemPath path, const ASTStruct& ent, const ASTAttributeList& attrs, HIRModule& outMod) {
    TRACE_FUNCTION_F(path);
    HIRStruct::Data data;

    auto modPath = getParentModule(path);
    auto getVis = [&](const ASTVisibility& vis) {
        return LowerHIRVis(modPath, vis);
    };

    auto rv = HIRStruct{LowerHIRGenericParams(ent.params(), nullptr), HIRStruct::Repr::Rust, {}};

    switch (ent.data.tag()) {
        case ASTStructData::TAG_Unit: {
            auto& e = ent.data.as_Unit();
            (void)e;
            rv.data = HIRStruct::Data::make_Unit({});
            break;
        }
        case ASTStructData::TAG_Tuple: {
            auto& e = ent.data.as_Tuple();
            HIRStruct::Data::Data_Tuple fields;

            for (const auto& field : e.ents) {
                fields.push_back({getVis(field.vis), LowerHIRType(field.type)});
            }

            rv.data = HIRStruct::Data::make_Tuple(mv$(fields));
            break;
        }
        case ASTStructData::TAG_Struct: {
            auto& e = ent.data.as_Struct();
            auto fields = LowerHIRStructFields(path, rv.params, e.ents, outMod);
            rv.data = HIRStruct::Data::make_Named(mv$(fields));
            break;
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
    // `PhantomData` is the escape hatch for an unused parameter, so it is the
    // one struct that may leave its own parameter unused.
    if (path.getSimplePath() != crate->getLangItemPathOpt("phantom_data")) {
        ::std::set<unsigned> used;
        bool opaque = false;
        switch (rv.data.tag()) {
            case HIRStructData::TAG_Unit: {
                auto& de = rv.data.as_Unit();
                (void)de;
                break;
            }
            case HIRStructData::TAG_Tuple: {
                auto& de = rv.data.as_Tuple();
                for (const auto& fld : de) {
                    collectUsedTypeParams(crate->types, sp, fld.ent, used, opaque);
                }
                break;
            }
            case HIRStructData::TAG_Named: {
                auto& de = rv.data.as_Named();
                for (const auto& fld : de) {
                    collectUsedTypeParams(crate->types, sp, fld.ty, used, opaque);
                }
                break;
            }
        }
        collectUsedTypeParamsInBounds(crate->types, sp, rv.params, used, opaque);
        if (!opaque) {
            checkTypeParamsUsed(sp, rv.params, used, "struct");
        }
    }
    rv.mustUse = attrs.has("must_use");
    rv.structMarkings.isFundamental = attrs.has("fundamental");
    const auto& simplePath = path.getSimplePath();
    rv.structMarkings.isNoNiche = simplePath == crate->getLangItemPathOpt("unsafe_cell") || simplePath == crate->getLangItemPathOpt("unsafe_pinned");
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
        if (const auto* d = rv.data.opt_Named()) {
            switch (d->size()) {
                case 2:
                    ty2 = (*d)[1].ty;
                case 1:
                    ty = (*d)[0].ty;
                    break;
            }
        } else if (const auto* d = rv.data.opt_Tuple()) {
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
        if (var.data.is_Tuple() || var.data.is_Struct()) {
            hasData = true;
        } else {
            // Unit-like
            assert(var.data.is_Unit());
        }
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
            variants.push_back({var.name, LowerHIRExpr(var.discriminantValue), U128(0)});
        }

        data = HIREnum::Class::make_Value({mv$(variants)});
    }
    // NOTE: empty enums are encoded as empty Data enums
    else {
        ::std::vector<HIREnum::DataVariant> variants;
        const auto variantRepr = isReprC || repr != HIREnum::Repr::Auto ? HIRStruct::Repr::C : HIRStruct::Repr::Rust;
        for (const auto& var : ent.variants()) {
            if (var.data.is_Unit() && ent.markings.alignValue == 0) {
                // TODO: Should this make its own unit-like struct?
                variants.push_back({var.name, false, crate->types.unit()});
            } else {
                HIRStruct::Data data;
                if (var.data.is_Unit()) {
                    data = HIRStruct::Data::make_Unit({});
                } else if (const auto* ve = var.data.opt_Tuple()) {
                    HIRStruct::Data::Data_Tuple fields;
                    for (const auto& field : ve->items) {
                        fields.push_back(newVisent(HIRPublicity::newGlobal(), LowerHIRType(field.type)));
                    }
                    data = HIRStruct::Data::make_Tuple(mv$(fields));
                } else if (const auto* ve = var.data.opt_Struct()) {
                    auto fields = LowerHIRStructFields(path, params, ve->fields, outMod);
                    data = HIRStruct::Data::make_Named(mv$(fields));
                } else {
                    throw "";
                }

                auto tyName = RcString::newInterned(FMT(path.name << "#" << var.name));
                auto variantStruct = HIRStruct{LowerHIRGenericParams(ent.params(), nullptr), variantRepr, mv$(data)};
                variantStruct.forcedAlignment = ent.markings.alignValue;
                pushStruct(tyName, mv$(variantStruct));
                auto tyIpath = path;
                tyIpath.name = tyName.c_str();
                auto tyPath = tyIpath.getFullPath();
                // Add type params
                tyPath.data.as_Generic().params = params.makeNopParams(crate->types, 0);
                variants.push_back({var.name, var.data.is_Struct(), crate->types.path(mv$(tyPath), {})});
            }

            if (var.discriminantValue) {
                // A fieldless enum only reaches this branch because
                // `#[repr(align(N))]` forced it to, and its discriminants are as
                // ordinary as any value enum's -- the alignment says nothing
                // about the tag.
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

    HIREnum rv{mv$(params), isReprC, repr, mv$(data)};
    rv.forcedAlignment = ent.markings.alignValue;
    rv.mustUse = attrs.has("must_use");
    return rv;
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
    for (const auto& field : f.variants) {
        variants.push_back(HIRStructField{field.name, getVis(field.vis), LowerHIRType(field.type), {}});
    }

    HIRUnion rv{LowerHIRGenericParams(f.params_, nullptr), repr, mv$(variants)};
    rv.forcedAlignment = f.markings.alignValue;
    rv.mustUse = attrs.has("must_use");
    return rv;
}

namespace {
    /// The return-position `impl Trait` types of a signature, outermost first --
    /// the order the binding pass numbers them in (`hir_conv_main_bindings.cpp`),
    /// so that the associated type named after a position is that position's.
    class RpititTypeCollector: public HIRVisitor {
        ::std::function<void(HIRTypeRef)> callback;

    public:
        RpititTypeCollector(HIRTypeInterner& types, ::std::function<void(HIRTypeRef)> callback)
            : HIRVisitor(nullptr, types)
            , callback(std::move(callback))
        {
        }

        void visitType(HIRTypeRef& ty) override {
            const auto* erased = ty->opt_ErasedType();
            if (erased && erased->inner.is_Fcn()) {
                callback(ty);
            }
            HIRVisitor::visitType(ty);
        }
    };

    /// Replace each nested `impl Trait` with the associated type that names it.
    /// A nested one belongs to no function of its own, so it cannot stay an
    /// erased type once it is copied out into a bound.
    class RpititNestedRewrite: public HIRVisitor {
        const ::std::map<HIRTypeRef, size_t>& indices;
        ::std::function<HIRTypeRef(size_t)> projection;

    public:
        RpititNestedRewrite(HIRTypeInterner& types, const ::std::map<HIRTypeRef, size_t>& indices, ::std::function<HIRTypeRef(size_t)> projection)
            : HIRVisitor(nullptr, types)
            , indices(indices)
            , projection(std::move(projection))
        {
        }

        void visitType(HIRTypeRef& ty) override {
            const auto index = indices.find(ty);
            if (index != indices.end()) {
                ty = projection(index->second);
                return;
            }
            HIRVisitor::visitType(ty);
        }
    };
}

HIRTrait AST2HIR::LowerHIRTrait(HIRSimplePath traitPath, const ASTTrait& f, const ASTAttributeList& attrs) {
    TRACE_FUNCTION_F(traitPath);
    traitPath.updateCrateName(crateName);

    bool traitReqiresSized = false;
    auto params = LowerHIRGenericParams(f.params(), &traitReqiresSized);

    ::std::vector<HIRTraitPath> supertraits;
    for (const auto& st : f.supertraits()) {
        supertraits.push_back(LowerHIRTraitPath(st.sp, *st.ent.path, st.ent.hrbs, true, st.ent.constness));
        DEBUG("Supertrait " << supertraits.back());
    }
    HIRTrait rv{mv$(params), mv$(supertraits)};
    rv.isConst = attrs.has("const_trait");
    rv.mustUse = attrs.has("must_use");
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
        thisTrait.params = rv.params.makeNopParams(crate->types, 0);
        rv.params.bounds.push_back(HIRGenericBound::make_TraitBound({crate->types.self(), HIRTraitPath(mv$(thisTrait))}));
    }

    for (const auto& item : f.items()) {
        auto traitIp = HIRItemPath(traitPath);
        auto itemPath = HIRItemPath(traitIp, item.name.c_str());

        switch (item.data.tag()) {
default:
            BUG(item.span, "Encountered unexpected item type in trait");
            case ASTItem::TAG_None: {
                auto& i = item.data.as_None();
                (void)i;
                // Ignore.
                break;
            }
            case ASTItem::TAG_MacroInv: {
                auto& i = item.data.as_MacroInv();
                (void)i;
                // Ignore.
                break;
            }
            case ASTItem::TAG_Type: {
                auto& i = item.data.as_Type();
                bool isSized = true;
                ::std::vector<HIRTraitPath> traitBounds;
                auto gps = LowerHIRGenericParams(i.params(), &isSized);

                // Bounds after an associated type declaration apply to that
                // associated type. Keep nested associated-type constraints on
                // the trait path instead of flattening them into predicates on
                // `Self`, as ordinary where-clause lowering does.
                for (const auto& bound : i.selfBounds.bounds) {
                switch (bound.tag()) {
                    case ASTGenericBound::TAG_None: {
                        auto& _ = bound.as_None();
                        (void)_;
                        break;
                    }
                    case ASTGenericBound::TAG_Lifetime: {
                        auto& _ = bound.as_Lifetime();
                        (void)_;
                        break;
                    }
                    case ASTGenericBound::TAG_TypeLifetime: {
                        auto& _ = bound.as_TypeLifetime();
                        (void)_;
                        break;
                    }
                    case ASTGenericBound::TAG_IsTrait: {
                        auto& be = bound.as_IsTrait();
                        auto type = LowerHIRType(be.type);
                        ASSERT_BUG(item.span, type == crate->types.self(), "Associated type bound has non-Self subject " << type);
                        ASSERT_BUG(item.span, be.outerHrbs.empty() || be.innerHrbs.empty(), "Two layers of higher-ranked binders in associated type bound");
                        auto trait = LowerHIRTraitPath(be.span, be.trait, be.innerHrbs, /*allow_bounds=*/true, be.constness);
                        if (trait.path.path == pathPointeeSized || trait.path.path == pathMetadataSized) {
                            isSized = false;
                        }
                        traitBounds.push_back(mv$(trait));
                        break;
                    }
                    case ASTGenericBound::TAG_MaybeTrait: {
                        auto& be = bound.as_MaybeTrait();
                        auto type = LowerHIRType(be.type);
                        ASSERT_BUG(item.span, type == crate->types.self(), "Associated type maybe-bound has non-Self subject " << type);
                        auto trait = LowerHIRGenericPath(item.span, be.trait, FromASTPathClass::Type);
                        if (trait.path == pathSized) {
                            isSized = false;
                        } else {
                            ERROR(item.span, E0000, "MaybeTrait on unknown trait " << trait.path);
                        }
                        break;
                    }
                    case ASTGenericBound::TAG_NotTrait: {
                        auto& _ = bound.as_NotTrait();
                        (void)_;
                        TODO(item.span, "Negative associated type bound");
                        break;
                    }
                    case ASTGenericBound::TAG_Equality: {
                        auto& _ = bound.as_Equality();
                        (void)_;
                        BUG(item.span, "Unexpected type equality bound on associated type");
                        break;
                    }
                }
                }
                rv.types.insert(::std::make_pair(item.name, HIRAssociatedType{mv$(gps), isSized, mv$(traitBounds), LowerHIRType(i.type())}));
                break;
            }
            case ASTItem::TAG_Function: {
                auto& i = item.data.as_Function();
                auto fcn = LowerHIRFunction(itemPath, traitPath.parent(), item.attrs, i, crate->types.self());
                ::std::vector<HIRTypeRef> erasedTypes;
                RpititTypeCollector(crate->types, [&](HIRTypeRef type) {
                    erasedTypes.push_back(type);
                }).visitType(fcn.returnType);
                ::std::map<HIRTypeRef, size_t> erasedIndices;
                for (size_t index = 0; index < erasedTypes.size(); index++) {
                    erasedIndices.insert(::std::make_pair(erasedTypes[index], index));
                }
                auto erasedName = [&](size_t index) {
                    return RcString::newInterned(FMT(ATY_PREFIX_ERASED << item.name << "_" << index));
                };
                RpititNestedRewrite rewrite{crate->types, erasedIndices, [&](size_t index) {
                                                return crate->types.path(
                                                    HIRPath(crate->types.self(), HIRGenericPath(traitPath, rv.params.makeNopParams(crate->types, 0)), erasedName(index), fcn.params.makeNopParams(crate->types, 1)), {});
                                            }};
                for (size_t index = 0; index < erasedTypes.size(); index++) {
                    const auto& erased = erasedTypes[index]->as_ErasedType();
                    ::std::vector<HIRTraitPath> bounds;
                    for (const auto& bound : erased.traits) {
                        bounds.push_back(bound.clone());
                        rewrite.visitTraitPath(bounds.back());
                    }
                    auto inserted = rv.types.insert(std::make_pair(erasedName(index), HIRAssociatedType{fcn.params.clone(), erased.isSized, std::move(bounds), crate->types.infer()}));
                    ASSERT_BUG(item.span, inserted.second, "Synthetic RPITIT associated type collides with " << erasedName(index));
                }
                if (rv.isConst) {
                    fcn.isConst = true;
                }
                fcn.saveCode = true;
                rv.values.insert(::std::make_pair(item.name, HIRTraitValueItem::make_Function(mv$(fcn))));
                break;
            }
            case ASTItem::TAG_Static: {
                auto& i = item.data.as_Static();
                if (i.sClass() == ASTStatic::CONST) {
                    auto constantParams = LowerHIRGenericParams(i.params(), nullptr);
                    rv.values.insert(::std::make_pair(item.name, HIRTraitValueItem::make_Constant(HIRConstant(mv$(constantParams), LowerHIRType(i.type()), LowerHIRExpr(i.value())))));
                } else {
                    HIRLinkage linkage;
                    rv.values.insert(::std::make_pair(item.name, HIRTraitValueItem::make_Static(HIRStatic(mv$(linkage), (i.sClass() == ASTStatic::MUT), LowerHIRType(i.type()), LowerHIRExpr(i.value())))));
                }
                break;
            }
        }
    }

    rv.isMarker = f.isMarker();
    rv.isCoinductive = rv.isMarker || attrs.has("rustc_coinductive");
    rv.isFundamental = attrs.has("fundamental");

    return rv;
}

HIRTraitAlias AST2HIR::LowerHIRTraitAlias(const Span& sp, HIRItemPath p, const ASTTraitAlias& f) {
    bool traitReqiresSized = false;

    HIRTraitAlias ta;
    ta.params = LowerHIRGenericParams(f.params, &traitReqiresSized);
    for (const auto& t : f.traits) {
        ta.traits.push_back(LowerHIRTraitPath(t.sp, *t.ent.path, t.ent.hrbs, false, t.ent.constness));
    }

    return ta;
}

::std::vector<HIRSimplePath> AST2HIR::LowerHIRDefineOpaque(HIRItemPath p, const HIRSimplePath& sourceModule, const ASTAttributeList& attrs) {
    ::std::vector<HIRSimplePath> defineOpaque;
    if (const auto* attr = attrs.get("define_opaque")) {
        TTStream tokens(attr->span(), ParseState(), attr->data());
        tokens.getTokenCheck(TOK_PAREN_OPEN);
        while (tokens.lookahead(0) != TOK_PAREN_CLOSE) {
            auto opaquePath = ParsePath(tokens, PATH_GENERIC_NONE);
            auto appendNodes = [&](HIRSimplePath path, const auto& nodes) {
                for (const auto& node : nodes) {
                    ASSERT_BUG(attr->span(), node.args().isEmpty(), "Generic path in #[define_opaque]");
                    path += node.name();
                }
                return path;
            };

            HIRSimplePath aliasPath;
            if (const auto* path = opaquePath.cls.opt_Absolute()) {
                aliasPath = appendNodes(HIRSimplePath(path->crate == "" ? crateName : path->crate), path->nodes);
            } else if (const auto* path = opaquePath.cls.opt_Relative()) {
                aliasPath = appendNodes(sourceModule.clone(), path->nodes);
            } else if (const auto* path = opaquePath.cls.opt_Self()) {
                aliasPath = appendNodes(sourceModule.clone(), path->nodes);
            } else if (const auto* path = opaquePath.cls.opt_Super()) {
                ASSERT_BUG(attr->span(), path->count <= sourceModule.components().size(), "Too many `super` components in #[define_opaque]");
                auto components = sourceModule.componentsVec();
                components.resize(components.size() - path->count);
                aliasPath = appendNodes(HIRSimplePath(sourceModule.crateName(), components), path->nodes);
            } else {
                ERROR(attr->span(), E0000, "Unsupported path in #[define_opaque]: " << opaquePath);
            }
            ASSERT_BUG(attr->span(), !aliasPath.components().empty(), "Empty path in #[define_opaque]");
            crate->opaqueTypeDefiners[aliasPath].push_back(p.getFullPath());
            defineOpaque.push_back(::std::move(aliasPath));
            if (!tokens.getTokenIf(TOK_COMMA)) {
                break;
            }
        }
        tokens.getTokenCheck(TOK_PAREN_CLOSE);
    }
    return defineOpaque;
}

HIRFunction AST2HIR::LowerHIRFunction(HIRItemPath p, const HIRSimplePath& sourceModule, const ASTAttributeList& attrs, const ASTFunction& f, const HIRTypeData* realSelfType) {
    static Span sp;

    TRACE_FUNCTION_F(p);

    auto defineOpaque = LowerHIRDefineOpaque(p, sourceModule, attrs);

    ::std::vector<::std::pair<HIRPattern, HIRTypeRef>> args;
    for (const auto& arg : f.args()) {
        // A parameter is matched unconditionally, so it cannot be a pattern
        // that only some values reach. A path names a variant or a unit struct,
        // which for a one-variant type is every value, and a range can cover a
        // whole integer type -- both are decided by exhaustiveness, not here.
        if (const auto* value = arg.pat.data().opt_Value()) {
            if (!value->start.is_Named() && value->end.is_Invalid()) {
                ERROR(arg.pat.span(), E0000, "refutable pattern in function argument");
            }
        }
        // Without a body there is nothing to destructure into: a declaration
        // names its parameters, it does not match them.
        if (!f.code().isValid() && !(arg.pat.data().is_Any() || arg.pat.data().is_MaybeBind())) {
            ERROR(arg.pat.span(), E0000, "patterns aren't allowed in functions without bodies");
        }
        args.push_back(::std::make_pair(LowerHIRPattern(arg.pat), LowerHIRType(arg.ty)));
    }

    auto receiver = HIRFunction::Receiver::Free;

    if (args.size() > 0 && args.front().first.bindings.size() > 0 && args.front().first.bindings[0].name == "self") {
        const auto& sp = f.args()[0].pat.span();
        auto& argSelfTy = args.front().second;

        struct Ivcr {
            AST2HIR& ctx;
            const Span& sp;
            const HIRTypeData* realSelfType;
            ::std::vector<HIRSimplePath> aliasStack;

            Ivcr(AST2HIR& ctx, const Span& sp, const HIRTypeData* realSelfType)
                : ctx(ctx)
                , sp(sp)
                , realSelfType(realSelfType)
            {
            }

            const HIRTypeItem* findTypeItem(const HIRSimplePath& path) const {
                const HIRModule* mod;
                if (path.crateName() == ctx.crate->crateName) {
                    mod = &ctx.crate->rootModule;
                } else {
                    auto crateIt = ctx.crate->extCrates.find(path.crateName());
                    if (crateIt == ctx.crate->extCrates.end()) {
                        return nullptr;
                    }
                    mod = &crateIt->second.data->rootModule;
                }
                for (size_t i = 0; i < path.components().size(); ++i) {
                    auto itemIt = mod->modItems.find(path.components()[i]);
                    if (itemIt == mod->modItems.end()) {
                        return nullptr;
                    }
                    if (i + 1 == path.components().size()) {
                        return &itemIt->second->ent;
                    }
                    mod = itemIt->second->ent.opt_Module();
                    if (!mod) {
                        return nullptr;
                    }
                }
                return nullptr;
            }

            bool isValidCustomReceiver(HIRTypeRef& ty) {
                // - The path must include Self as a (the only?) type param.
                if (ty == ctx.crate->types.self()) {
                    return true;
                } else if (ty == realSelfType) {
                    ty = ctx.crate->types.self();
                    return true;
                } else if (ty->is_Path()) {
                    auto data = ty->cloneData();
                    auto& e = data.as_Path();
                    if (auto* pe = e.path.data.opt_Generic()) {
                        const auto* item = findTypeItem(pe->path);
                        if (item && item->is_TypeAlias()) {
                            if (::std::find(aliasStack.begin(), aliasStack.end(), pe->path) != aliasStack.end()) {
                                return false;
                            }
                            aliasStack.push_back(pe->path);
                            auto expanded = ConvertHIRExpandTypeAlias(sp, *ctx.crate, *pe, false);
                            auto valid = isValidCustomReceiver(expanded);
                            aliasStack.pop_back();
                            if (valid) {
                                ty = expanded;
                                return true;
                            }
                            return false;
                        }
                        if (pe->params.types.size() == 0) {
                            // A receiver that names no type reaches `Self`
                            // through its `Receiver`/`Deref` impl instead, which
                            // is not known until impls are indexed.
                            return true;
                        }
                        //   TODO(sp, "Receiver types with more than one param - " << arg_self_ty);
                        //}

                        // TODO: Allow if the type parm is a valid receiver it type too
                        // - In general, it's valid if there's a deref chain from this type to `self` (maybe could check that in a later pass, instead of erroring here)
                        if (isValidCustomReceiver(pe->params.types[0])) {
                            ty = ctx.crate->types.intern(mv$(data));
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
                    ty = ctx.crate->types.borrow(e.type, inner);
                    return true;
                } else if (ty->is_Pointer()) {
                    const auto& e = ty->as_Pointer();
                    auto inner = e.inner;
                    if (!isValidCustomReceiver(inner)) {
                        return false;
                    }
                    ty = ctx.crate->types.pointer(e.type, inner);
                    return true;
                } else if (ty->is_Generic()) {
                    // `self: T` (or `self: &T`) is a receiver by way of
                    // `T: Deref<Target = Self>`, a bound that is checked later.
                    return true;
                } else {
                    return false;
                }
            }
        } ivcr(*this, sp, realSelfType);

        if (argSelfTy == crate->types.self() || argSelfTy == realSelfType) {
            receiver = HIRFunction::Receiver::Value;
        } else if (const auto* e = argSelfTy->opt_Borrow()) {
            if (e->inner == crate->types.self() || e->inner == realSelfType) {
                if (e->inner == realSelfType) {
                    argSelfTy = crate->types.borrow(e->type, crate->types.self());
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
                    argSelfTy = crate->types.borrow(e->type, inner);
                    receiver = HIRFunction::Receiver::Custom;
                }
            }
        } else if (const auto* e = argSelfTy->opt_Path()) {
            // Box - Compare with `owned_box` lang item
            if (const auto* pe = e->path.data.opt_Generic()) {
                auto p = crate->getLangItemPathOpt("owned_box");
                if (pe->path == p) {
                    if (pe->params.types.size() >= 1 && (pe->params.types[0] == crate->types.self() || pe->params.types[0] == realSelfType)) {
                        if (pe->params.types[0] == realSelfType) {
                            auto data = argSelfTy->cloneData();
                            data.as_Path().path.data.as_Generic().params.types[0] = crate->types.self();
                            argSelfTy = crate->types.intern(mv$(data));
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
            } else if (e->path.data.is_UfcsKnown()) {
                // Associated type projections need the complete impl set, so
                // their receiver relation is checked after HIR lowering.
                receiver = HIRFunction::Receiver::Custom;
            }
        } else if (argSelfTy->is_Generic()) {
            // `fn f(self: T)` is a receiver by way of `T: Deref<Target = Self>`,
            // and bounds are checked after lowering.
            receiver = HIRFunction::Receiver::Custom;
        } else if (ivcr.isValidCustomReceiver(argSelfTy)) {
            receiver = HIRFunction::Receiver::Custom;
        } else {
        }

        if (receiver == HIRFunction::Receiver::Free && visitTyWith(argSelfTy, [](const HIRTypeData* ty) {
                const auto* path = ty->opt_Path();
                return path && path->path.data.is_UfcsKnown();
            })) {
            // A projection nested inside Box/Rc/etc. needs the same deferred
            // normalization as a projection used directly as the receiver.
            receiver = HIRFunction::Receiver::Custom;
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
    // #[must_use] - The caller has to use the return value
    markings.mustUse = attrs.has("must_use");
    markings.isNaked = f.markings.isNaked;
    // Lint levels set on this function, which the lints consult before the
    // crate's own settings.
    markings.lintLevels = f.markings.lintLevels;
    markings.lintGroupLevels = f.markings.lintGroupLevels;
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
    if (astCrate->testHarness && f.code().isValid()) {
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
        rv.receiverType = MonomorphiserNop(crate->types).monomorphType(f.args()[0].ty->span(), args.front().second, false);
    }
    rv.abi = RcString::newInterned(f.abi());
    rv.unsafe = f.isUnsafe();
    rv.isConst = f.isConst();
    rv.params = LowerHIRGenericParams(f.params(), nullptr); // TODO: If this is a method, then it can add the Self: Sized bound
    rv.args = mv$(args);
    rv.variadic = f.isVariadic();
    rv.returnType = LowerHIRType(f.rettype());
    rv.source = SourceLocation(f.sp());
    rv.code = LowerHIRExpr(f.code());
    // A parameter matched by `!` takes a value of a type that has none, so the
    // function is never entered. Its body is unreachable whatever it says, and
    // whatever the return type is: replace it with a body that diverges.
    if (rv.code) {
        bool neverArg = false;
        for (const auto& arg : f.args()) {
            neverArg |= PatternContainsNever(arg.pat);
        }
        if (neverArg) {
            auto* body = crate->pool->make<HIRExprNodeBlock>(f.sp());
            body->resType = crate->types.unit();
            auto* loop = crate->pool->make<HIRExprNodeLoop>(f.sp(), RcString(), HIRExprNodeP(body));
            loop->resType = crate->types.infer();
            rv.code = HIRExprPtr(loop);
        }
    }
    rv.defineOpaque = ::std::move(defineOpaque);
    rv.markings = markings;

    if (f.isGen() && f.isAsync()) {
        // `async gen fn`: the declared type is the item type, and the body is a
        // coroutine that awaits and yields instead of returning a value.
        auto itemType = rv.returnType;
        if (rv.code) {
            auto* asyncNode = crate->pool->make<HIRExprNodeAsyncBlock>(sp, crate->types.unit(), rv.code.takeNode(), true, false);
            asyncNode->isAsyncGen = true;
            asyncNode->yieldTy = itemType;
            asyncNode->resType = crate->types.infer();
            rv.code = HIRExprPtr(HIRExprNodeP(asyncNode));
        }
        // Make the return type be `impl AsyncIterator<Item=Ret>`
        HIRTraitPath iteratorPath;
        iteratorPath.path.path = crate->getLangItemPath(sp, "async_iterator");
        iteratorPath.typeBounds.insert(std::make_pair(RcString::newInterned("Item"), HIRTraitPath::AtyEqual{iteratorPath.path.clone(), {}, itemType}));
        rv.returnType = crate->types.intern(HIRTypeData::make_ErasedType(HIRTypeDataErasedType{true, ::makeVec1(std::move(iteratorPath)), TypeDataErasedTypeInner::Data_Fcn{HIRPath(HIRSimplePath()), 0}}));
        return rv;
    }

    if (f.isGen()) {
        // The body is the coroutine, and the function hands back the iterator
        // over it -- the same shape a `gen { .. }` block lowers to. A coroutine
        // body yields, so it returns nothing itself.
        if (rv.code) {
            auto* coroutine = crate->pool->make<HIRExprNodeGenerator>(sp, crate->types.unit(), crate->types.infer(), HIRPattern(), false, crate->types.infer(), rv.code.takeNode(), true, false, false);
            coroutine->resType = crate->types.infer();
            HIRExprNodeP node(coroutine);
            node.reset(crate->pool->make<HIRExprNodeCallPath>(sp, HIRSimplePath(coreCrate, {"iter", "sources", "from_coroutine", "from_coroutine"}), makeVec1(mv$(node))));
            node->resType = crate->types.infer();
            node.reset(crate->pool->make<HIRExprNodeCallMethod>(sp, mv$(node), RcString::newInterned("fuse"), HIRPathParams(), ::std::vector<HIRExprNodeP>()));
            node->resType = crate->types.infer();
            rv.code = HIRExprPtr(mv$(node));
        }
        // Make the return type be `impl Iterator<Item=Ret>`
        HIRTraitPath iteratorPath;
        iteratorPath.path.path = crate->getLangItemPath(sp, "iterator");
        iteratorPath.typeBounds.insert(std::make_pair(RcString::newInterned("Item"), HIRTraitPath::AtyEqual{iteratorPath.path.clone(), {}, std::move(rv.returnType)}));
        rv.returnType = crate->types.intern(HIRTypeData::make_ErasedType(HIRTypeDataErasedType{true, ::makeVec1(std::move(iteratorPath)), TypeDataErasedTypeInner::Data_Fcn{HIRPath(HIRSimplePath()), 0}}));
    }

    if (f.isAsync()) {
        // Wrap the code in an async block
        if (rv.code) {
            auto* asyncNode = crate->pool->make<HIRExprNodeAsyncBlock>(sp, rv.returnType, rv.code.takeNode(), true, false);
            asyncNode->resType = crate->types.infer();
            rv.code = HIRExprPtr(HIRExprNodeP(asyncNode));
        }
        // Make the return type be `impl Future<Output=Ret>`
        HIRTraitPath futurePath;
        futurePath.path.path = crate->getLangItemPath(sp, "future_trait");
        futurePath.typeBounds.insert(std::make_pair(RcString::newInterned("Output"), HIRTraitPath::AtyEqual{futurePath.path.clone(), {}, std::move(rv.returnType)}));
        rv.returnType = crate->types.intern(HIRTypeData::make_ErasedType(HIRTypeDataErasedType{true, ::makeVec1(std::move(futurePath)), TypeDataErasedTypeInner::Data_Fcn{HIRPath(HIRSimplePath()), 0}}));
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

    auto params = LowerHIRGenericParams(e.params(), nullptr);
    auto value = LowerHIRExpr(e.value());
    value.defineOpaque = LowerHIRDefineOpaque(p, p.parent->getSimplePath(), attrs);

    if (e.sClass() == ASTStatic::CONST) {
        // Note: Empty names are allowed for `const _: ...`
        return HIRValueItem::make_Constant(HIRConstant(mv$(params), LowerHIRType(e.type()), mv$(value)));
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

        return HIRValueItem::make_Static(HIRStatic(mv$(linkage), (e.sClass() == ASTStatic::MUT), LowerHIRType(e.type()), mv$(value)));
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
            AST2HIR& ctx;
            HIRModule& mod;

            Foo(AST2HIR& ctx, HIRModule& mod)
                : ctx(ctx)
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
                    if (const auto* pbe = e.ent.path->bindings.type.binding.opt_TraitAlias()) {
                        pushTraitAlias(*pbe);
                    } else {
                        pushTrait(ctx.LowerHIRSimplePath(e.sp, *e.ent.path, FromASTPathClass::Type, true));
                    }
                }
            }

            void pushTraitAliasHir(const HIRTraitAlias& ta) {
                for (const auto& p : ta.traits) {
                    if (const auto* tap = ctx.crate->getTypeitemByPath(Span(), p.path.path).opt_TraitAlias()) {
                        pushTraitAliasHir(*tap);
                    } else {
                        pushTrait(p.path.path);
                    }
                }
            }
        };

        Foo f{*this, mod};
        for (const auto& traitPath : astMod.traits) {
            f.pushTrait(HIRSimplePath((traitPath.crate == "" ? crateName : traitPath.crate), traitPath.nodes));
        }
        for (const auto& i : astMod.typeItems) {
            if (const auto* pbe = i.second.path.bindings.type.binding.opt_TraitAlias()) {
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
            _add_mod_ns_item(*crate->pool, mod, mv$(name), HIRPublicity::newPriv(modPath), mv$(ti));
        }
    }

    for (const auto& ip : astMod.items) {
        const auto& item = *ip;
        const auto& sp = item.span;
        auto itemPath = HIRItemPath(path, item.name.c_str());
        DEBUG(itemPath << " " << item.data.tagStr());
        switch (item.data.tag()) {
            case ASTItem::TAG_None: {
                auto& e = item.data.as_None();
                (void)e;
                break;
            }
            case ASTItem::TAG_Macro: {
                auto& e = item.data.as_Macro();
                (void)e;
                // NOTE: These are in `m_macros`
                break;
            }
            case ASTItem::TAG_MacroInv: {
                auto& e = item.data.as_MacroInv();
                (void)e;
                // Valid.
                break;
            }
            case ASTItem::TAG_GlobalAsm: {
                auto& e = item.data.as_GlobalAsm();
                HIRGlobalAssembly item;
                item.span = sp;
                item.lines = std::move(e.lines);
                item.operands.reserve(e.operands.size());
                for (auto& operand : e.operands) {
                    switch (operand.tag()) {
                        case ASTGlobalAsmOperand::TAG_Const: {
                            auto& expr = operand.as_Const();
                            auto value = LowerHIRConstGeneric(*expr);
                            ASSERT_BUG(sp, value.is_Unevaluated(), "global_asm const operand lowered without an expression");
                            const auto* type = (*value.as_Unevaluated()->expr)->resType;
                            item.operands.push_back(HIRGlobalAsmOperand::make_Const({std::move(value), type}));
                            break;
                        }
                        case ASTGlobalAsmOperand::TAG_Sym: {
                            auto& sym = operand.as_Sym();
                            item.operands.push_back(HIRGlobalAsmOperand::make_Sym(LowerHIRPath(sp, sym, FromASTPathClass::Value)));
                            break;
                        }
                    }
                }
                item.options = e.options;
                mod.globalAsm.push_back(std::move(item));
                break;
            }
            case ASTItem::TAG_ExternBlock: {
                auto& e = item.data.as_ExternBlock();
                if (e.items().size() > 0) {
                    TODO(sp, "Expand ExternBlock");
                }
                for (const auto& lib : e.libraries) {
                    crate->extLibs.push_back(HIRExternLibrary{lib.libName});
                }
                break;
            }
            case ASTItem::TAG_Impl: {
                auto& e = item.data.as_Impl();
                (void)e;
                // NOTE: impl blocks are handled in a second pass
                break;
            }
            case ASTItem::TAG_NegImpl: {
                auto& e = item.data.as_NegImpl();
                (void)e;
                // NOTE: impl blocks are handled in a second pass
                break;
            }
            case ASTItem::TAG_Use: {
                auto& e = item.data.as_Use();
                (void)e;
                // Ignore - The index is used to add `Import`s
                break;
            }
            case ASTItem::TAG_Module: {
                auto& e = item.data.as_Module();
                _add_mod_ns_item(*crate->pool, mod, item.name, getVis(item.vis), LowerHIRModule(e, mv$(itemPath)));
                break;
            }
            case ASTItem::TAG_Crate: {
                auto& e = item.data.as_Crate();
                // All 'extern crate' items should be normalised into a list in the crate root
                // - If public, add a namespace import here referring to the root of the imported crate
                _add_mod_ns_item(*crate->pool, mod, item.name, getVis(item.vis), HIRTypeItem::make_Import({HIRSimplePath(e.name, {}), false, 0}));
                break;
            }
            case ASTItem::TAG_Type: {
                auto& e = item.data.as_Type();
                if (e.type()->data.is_Any()) {
                    if (!e.params().params.empty() || !e.params().bounds.empty()) {
                        ERROR(item.span, E0000, "Generics on extern type");
                    }
                    _add_mod_ns_item(*crate->pool, mod, item.name, getVis(item.vis), HIRExternType{});
                    break;
                }
                _add_mod_ns_item(*crate->pool, mod, item.name, getVis(item.vis), HIRTypeItem::make_TypeAlias(LowerHIRTypeAlias(itemPath, e)));
                break;
            }
            case ASTItem::TAG_Struct: {
                auto& e = item.data.as_Struct();
                /// Add value reference
                if (e.data.is_Unit()) {
                    _add_mod_val_item(*crate->pool, mod, item.name, getVis(item.vis), HIRValueItem::make_StructConstant({itemPath.getSimplePath()}));
                } else if (e.data.is_Tuple()) {
                    _add_mod_val_item(*crate->pool, mod, item.name, getVis(item.vis), HIRValueItem::make_StructConstructor({itemPath.getSimplePath()}));
                } else {
                }
                _add_mod_ns_item(*crate->pool, mod, item.name, getVis(item.vis), LowerHIRStruct(ip->span, itemPath, e, item.attrs, mod));
                break;
            }
            case ASTItem::TAG_Enum: {
                auto& e = item.data.as_Enum();
                auto enm = LowerHIREnum(itemPath, e, item.attrs, [&](auto name, auto str) {
                    _add_mod_ns_item(*crate->pool, mod, name, getVis(item.vis), mv$(str));
                }, mod);
                _add_mod_ns_item(*crate->pool, mod, item.name, getVis(item.vis), mv$(enm));
                break;
            }
            case ASTItem::TAG_Union: {
                auto& e = item.data.as_Union();
                _add_mod_ns_item(*crate->pool, mod, item.name, getVis(item.vis), LowerHIRUnion(itemPath, e, item.attrs));
                break;
            }
            case ASTItem::TAG_Trait: {
                auto& e = item.data.as_Trait();
                _add_mod_ns_item(*crate->pool, mod, item.name, getVis(item.vis), LowerHIRTrait(itemPath.getSimplePath(), e, item.attrs));
                break;
            }
            case ASTItem::TAG_TraitAlias: {
                auto& e = item.data.as_TraitAlias();
                _add_mod_ns_item(*crate->pool, mod, item.name, getVis(item.vis), LowerHIRTraitAlias(sp, itemPath, e));
                break;
            }
            case ASTItem::TAG_Function: {
                auto& e = item.data.as_Function();
                _add_mod_val_item(*crate->pool, mod, item.name, getVis(item.vis), LowerHIRFunction(itemPath, modPath, item.attrs, e, HIRTypeRef{}));
                break;
            }
            case ASTItem::TAG_Static: {
                auto& e = item.data.as_Static();
                _add_mod_val_item(*crate->pool, mod, item.name, getVis(item.vis), LowerHIRStatic(itemPath, item.attrs, e, sp, item.name));
                break;
            }
        }
    }
    // Some explicit handling of mac
    for (auto& mac : const_cast<ASTModule&>(astMod).macros()) {
        if (mac.data || mac.vis.isGlobal()) {
            ASSERT_BUG(mac.span, mac.data, "Null macro - " << mac.name);
            ASSERT_BUG(mac.span, mac.data->rules.size() > 0, "Empty macro - " << mac.name);
            _add_mod_mac_item(*crate->pool, mod, mac.name, getVis(mac.vis), std::move(mac.data));
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
            if (const auto* pb = ie.second.path.bindings.type.binding.opt_EnumVar()) {
                DEBUG("Import NS " << ie.first << " = " << hirPath << " (Enum Variant)");
                ti = HIRTypeItem::make_Import({mv$(hirPath), true, pb->idx});
            } else {
                DEBUG("Import NS " << ie.first << " = " << hirPath);
                ti = HIRTypeItem::make_Import({mv$(hirPath), false, 0});
            }
            _add_mod_ns_item(*crate->pool, mod, ie.first, getVis(ie.second.vis), mv$(ti));
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

            switch (ie.second.path.bindings.value.binding.tag()) {
default:
                DEBUG("Import VAL " << ie.first << " = " << hirPath);
                vi = HIRValueItem::make_Import({mv$(hirPath), false, 0});
                break;
                case ASTPathBindingValue::TAG_EnumVar: {
                    auto& pb = ie.second.path.bindings.value.binding.as_EnumVar();
                    DEBUG("Import VAL " << ie.first << " = " << hirPath << " (Enum Variant)");
                    vi = HIRValueItem::make_Import({mv$(hirPath), true, pb.idx});
                    break;
                }
            }
            _add_mod_val_item(*crate->pool, mod, ie.first, getVis(ie.second.vis), mv$(vi));
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
            _add_mod_mac_item(*crate->pool, mod, ie.first, getVis(ie.second.vis), mv$(mi));
        } else {
            DEBUG("Defined MACRO " << ie.first << " = " << hirPath);
        }
    }

    return mod;
}

void AST2HIR::LowerHIRModuleImpls(const ASTModule& astMod, HIRCrate& hirCrate) {
    TRACE_FUNCTION_F(astMod.path());
    HIRSimplePath modPath(crateName, astMod.path().nodes);

    // Sub-modules
    for (const auto& item : astMod.items) {
        if (const auto* e = item->data.opt_Module()) {
            LowerHIRModuleImpls(*e, hirCrate);
        }
    }
    for (const auto& submodPtr : astMod.anonMods()) {
        if (submodPtr) {
            LowerHIRModuleImpls(*submodPtr, hirCrate);
        }
    }

    for (const auto& i : astMod.items) {
        if (!i->data.is_Impl()) {
            continue;
        }
        const auto& impl = i->data.as_Impl();
        const Span implSpan;
        auto params = LowerHIRGenericParams(impl.def().params(), nullptr);

        TRACE_FUNCTION_F("IMPL " << impl.def());

        if (impl.def().trait().ent.isValid()) {
            const auto& pb = impl.def().trait().ent.bindings.type.binding;
            ASSERT_BUG(Span(), pb.is_Trait(), "Binding for trait path in impl isn't a Trait - " << impl.def().trait().ent);
            ASSERT_BUG(Span(), pb.as_Trait().trait_ || pb.as_Trait().hir, "Trait pointer for trait path in impl isn't set");
            bool isMarker = (pb.as_Trait().trait_ ? pb.as_Trait().trait_->isMarker() : pb.as_Trait().hir->isMarker);
            auto traitPath = LowerHIRGenericPath(impl.def().trait().sp, impl.def().trait().ent, FromASTPathClass::Type);
            auto traitName = mv$(traitPath.path);
            auto traitArgs = mv$(traitPath.params);

            if (!isMarker) {
                auto type = LowerHIRType(impl.def().type());

                HIRItemPath path(type, traitName, traitArgs);
                DEBUG("path = " << path);

                ::std::map<RcString, HIRTraitImpl::ImplEnt<HIRFunction>> methods;
                ::std::map<RcString, HIRTraitImpl::ImplEnt<HIRConstant>> constants;
                ::std::map<RcString, HIRTraitImpl::ImplEnt<HIRTypeRef>> types;

                for (const auto& item : impl.items()) {
                    HIRItemPath itemPath(path, item.name.c_str());
                    switch ((*item.data).tag()) {
default:
                        BUG(item.sp, "Unexpected item type in trait impl - " << item.data->tagStr());
                        case ASTItem::TAG_None: {
                            auto& e = (*item.data).as_None();
                            (void)e;
                            break;
                        }
                        case ASTItem::TAG_MacroInv: {
                            auto& e = (*item.data).as_MacroInv();
                            (void)e;
                            break;
                        }
                        case ASTItem::TAG_Static: {
                            auto& e = (*item.data).as_Static();
                            if (e.sClass() == ASTStatic::CONST) {
                                // TODO: Check signature against the trait?
                                auto constantParams = LowerHIRGenericParams(e.params(), nullptr);
                                constants.insert(::std::make_pair(item.name, HIRTraitImpl::ImplEnt<HIRConstant>{item.isSpecialisable, HIRConstant(mv$(constantParams), LowerHIRType(e.type()), LowerHIRExpr(e.value()))}));
                            } else {
                                TODO(item.sp, "Associated statics in trait impl");
                            }
                            break;
                        }
                        case ASTItem::TAG_Type: {
                            auto& e = (*item.data).as_Type();
                            DEBUG("- type " << item.name);
                            auto atyParams = LowerHIRGenericParams(e.params(), nullptr);
                            //ASSERT_BUG(Span(), aty_params.is_empty(), "TODO: GATs");

                            assert(!implTraitSource.path);
                            HIRItemPath ip1(modPath);
                            ::std::string name2 = ::std::string("#impl_") + ::std::to_string((uintptr_t)&impl) + "_" + item.name.c_str();
                            HIRItemPath ip2(ip1, name2.c_str());
                            implTraitSource = ImplTraitSource(&ip2, &params, &atyParams);

                            types.insert(::std::make_pair(item.name, HIRTraitImpl::ImplEnt<HIRTypeRef>{item.isSpecialisable, LowerHIRType(e.type())}));

                            implTraitSource = ImplTraitSource();
                            break;
                        }
                        case ASTItem::TAG_Function: {
                            auto& e = (*item.data).as_Function();
                            DEBUG("- method " << item.name);
                            auto fcn = LowerHIRFunction(itemPath, modPath, item.attrs, e, type);
                            if (impl.def().isConst()) {
                                fcn.isConst = true;
                            }
                            methods.insert(::std::make_pair(item.name, HIRTraitImpl::ImplEnt<HIRFunction>{item.isSpecialisable, mv$(fcn)}));
                            break;
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
            } else if (impl.def().type()->data.is_None()) {
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
                switch ((*item.data).tag()) {
default:
                    BUG(item.sp, "Unexpected item type in inherent impl - " << item.data->tagStr());
                    case ASTItem::TAG_None: {
                        auto& e = (*item.data).as_None();
                        (void)e;
                        break;
                    }
                    case ASTItem::TAG_MacroInv: {
                        auto& e = (*item.data).as_MacroInv();
                        (void)e;
                        break;
                    }
                    case ASTItem::TAG_Static: {
                        auto& e = (*item.data).as_Static();
                        if (e.sClass() == ASTStatic::CONST) {
                            auto constantParams = LowerHIRGenericParams(e.params(), nullptr);
                            constants.insert(::std::make_pair(item.name, HIRTypeImpl::VisImplEnt<HIRConstant>{getVis(item.vis), item.isSpecialisable, HIRConstant(mv$(constantParams), LowerHIRType(e.type()), LowerHIRExpr(e.value()))}));
                        } else {
                            TODO(item.sp, "Associated statics in inherent impl");
                        }
                        break;
                    }
                    case ASTItem::TAG_Type: {
                        auto& e = (*item.data).as_Type();
                        DEBUG("- type " << item.name);
                        auto atyParams = LowerHIRGenericParams(e.params(), nullptr);

                        assert(!implTraitSource.path);
                        implTraitSource = ImplTraitSource(&itemPath, &params, &atyParams);
                        auto atyType = LowerHIRType(e.type());
                        implTraitSource = ImplTraitSource();

                        types.insert(::std::make_pair(item.name, HIRTypeImpl::VisImplEnt<HIRTypeAlias>{getVis(item.vis), item.isSpecialisable, HIRTypeAlias{mv$(atyParams), mv$(atyType)}}));
                        break;
                    }
                    case ASTItem::TAG_Function: {
                        auto& e = (*item.data).as_Function();
                        methods.insert(::std::make_pair(item.name, HIRTypeImpl::VisImplEnt<HIRFunction>{getVis(item.vis), item.isSpecialisable, LowerHIRFunction(itemPath, modPath, item.attrs, e, type)}));
                        break;
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
    for (const auto& i : astMod.items) {
        if (!i->data.is_NegImpl()) {
            continue;
        }
        const auto& impl = i->data.as_NegImpl();

        auto params = LowerHIRGenericParams(impl.params(), nullptr);
        auto type = LowerHIRType(impl.type());
        auto trait = LowerHIRGenericPath(impl.trait().sp, impl.trait().ent, FromASTPathClass::Type);
        auto traitName = mv$(trait.path);
        auto traitArgs = mv$(trait.params);

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
                e->trait.traitPtr = &this->crate.getTraitByPath(nullSpan, e->trait.path.path);
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
    this->wb = &wb;
    auto& rv = *pool->make<HIRCrate>(pool, crate.types);

    if (crate.crateType != ASTCrate::Type::Executable) {
        rv.crateName = crate.crateNameReal;
    } else {
        // Use a non-empty crate name that won't conflict with any libraries
        rv.crateName = "bin#";
    }
    {
        // The test harness names the crate `X$test` so that its symbols do not
        // collide with the crate it is built from; the type name is the name
        // the crate was written under.
        ::std::string display(crate.crateNameSet);
        static const ::std::string testSuffix = "$test";
        if (display.size() > testSuffix.size()
            && display.compare(display.size() - testSuffix.size(), testSuffix.size(), testSuffix) == 0) {
            display.resize(display.size() - testSuffix.size());
        }
        rv.crateNameDisplay = RcString::newInterned(display);
    }
    rv.edition = crate.edition;
    rv.isNoCore = crate.loadStd == ASTCrate::LOAD_NONE;
    rv.noMain = crate.noMain;
    rv.features = crate.features;

    this->crate = &rv;
    astCrate = &crate;
    crateName = rv.crateName;
    coreCrate = crate.extCratenameCore;
    wb.settings->crateName = crateName;
    wb.settings->coreCrate = coreCrate;
    auto macros = std::map<RcString, HIRMacroItem>();

    // - Extract exported macros
    {
        TRACE_FUNCTION_FR("macros", "macros");
        ::std::vector<ASTModule*> mods;
        mods.push_back(&crate.rootModule_);
        do {
            auto& mod = *mods.back();
            mods.pop_back();

            for (/*const*/ auto& mac : mod.macros()) {
                if (mac.data->exported) {
                    HIRMacroItem mi;
                    if (&mod == &crate.rootModule_) {
                        mi = mv$(mac.data);
                    } else {
                        assert(mac.data);
                        assert(!mac.data->rules.empty());
                        auto pc = mod.path().nodes;
                        pc.push_back(mac.name);
                        mi = HIRMacroItem::make_Import({HIRSimplePath(crateName, std::move(pc))});
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

            for (auto& i : mod.items) {
                if (i->data.is_Module()) {
                    mods.push_back(&i->data.as_Module());
                }
            }
        } while (mods.size() > 0);

        for (const auto& mac : crate.rootModule_.macroImports) {
            if (mac.isPub || (mac.ref.is_MacroRules() && mac.ref.as_MacroRules()->exported)) {
                // Add to the re-export list
                auto path = HIRSimplePath(mac.path.crate == "" ? crateName : mac.path.crate, mac.path.nodes);
                auto res = macros.insert(std::make_pair(mac.name, HIRMacroItem::make_Import({path})));
                if (!res.second) {
                    DEBUG("Conflict in imported vs local macros: " << mac.name);
                } else {
                    DEBUG("Re-export " << mac.name << "! = " << path);
                    rv.exportedMacroNames.push_back(mac.name);
                }
            }
        }

        for (const auto& i : crate.rootModule_.macroItems) {
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
    for (const auto& langItemPath : crate.langItems) {
        assert(langItemPath.second.crate == "");
        rv.langItems.insert(::std::make_pair(langItemPath.first, HIRSimplePath(crateName, langItemPath.second.nodes)));
        DEBUG("Defined language item '" << langItemPath.first << "' at " << langItemPath.second);
    }
    rv.extCratesOrdered = crate.externCratesOrd;
    for (auto& extCrate : crate.externCrates) {
        // Populate m_lang_items from loaded crates too
        for (const auto& lang : extCrate.second.hir->langItems) {
            const auto& name = lang.first;
            const auto& path = lang.second;
            auto irv = rv.langItems.insert(::std::make_pair(name, path));
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

    rv.rootModule = LowerHIRModule(crate.rootModule_, HIRItemPath(rv.crateName));
    for (auto& e : macros) {
        if (e.second.is_MacroRules()) {
            ASSERT_BUG(Span(), !e.second.as_MacroRules()->rules.empty(), "Empty macro? - " << e.first);
        }
        rv.rootModule.macroItems.insert(::std::make_pair(e.first, rv.pool->make<HIRVisEnt<HIRMacroItem>>(HIRVisEnt<HIRMacroItem>{HIRPublicity::newGlobal(), mv$(e.second)})));
    }

    LowerHIRModuleImpls(crate.rootModule_, rv);

    // Set all pointers in the HIR to the correct (now fixed) locations

    // Macro fixups:
    // - Convert interpolated AST items to token sequences
    {
        struct H {
            AST2HIR& ctx;
            explicit H(AST2HIR& ctx) : ctx(ctx) {}
            void fixMacroContents(std::vector<MacroExpansionEnt>& ruleContents) {
                MacroRulesNormaliseFragments(*ctx.wb, ruleContents);
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
                            mr.sourceCrate = ctx.crateName;
                        }
                        for (auto& rule : mr.rules) {
                            fixMacroContents(rule.contents);
                        }
                    }
                    if (const auto* i = mi.second->ent.opt_Import()) {
                        DEBUG(path << ": Import " << mi.first << " = " << i->path);
                        if (i->path.crateName() == CRATE_BUILTINS) {
                        } else if (const auto* i2 = ctx.crate->getMacroitemByPath(Span(), i->path).opt_Import()) {
                            BUG(Span(), "Attempted recusive import - " << i->path << " points at " << i2->path);
                        }
                    }
                }
            }
        };

        H(*this).fixMacrosInMod(HIRItemPath(""), rv.rootModule);
    }

    if (coreCrate == "") {
        coreCrate = crateName;
        wb.settings->coreCrate = coreCrate;
    }

    this->crate = nullptr;
    return &rv;
}

struct LowerHIRExprNodeVisitor: public ASTNodeVisitor {
    AST2HIR& ctx;
    explicit LowerHIRExprNodeVisitor(AST2HIR& ctx) : ctx(ctx) {}

    HIRExprNodeP rv;

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
    bool hasYield = false;

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
                targetHygiene.leaveMacroDefinition(*ctx.crate->pool, definition.definitionId, definition.tokenHygiene, definition.definitionHygiene);
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
        ASSERT_BUG(ep->span(), rv, ep.typeName() << " - Yielded a nullptr HIR node");
        rv->resType = ctx.crate->types.infer();
        return std::move(rv);
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
        auto rv = ctx.crate->pool->make<HIRExprNodeBlock>(v.span());
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
            rv->localMod = HIRSimplePath(ctx.crateName, v.localMod->path().nodes);
        }

        switch (v.blockType) {
            case ASTExprNodeBlock::Type::Bare:
                break;
            case ASTExprNodeBlock::Type::Unsafe:
                rv->isUnsafe = true;
                break;
            case ASTExprNodeBlock::Type::Const:
                break;
        }

        if (label != "") {
            // A labelled block runs once: it is a loop that always breaks at the
            // end, carrying the block's tail value if it has one. Without that
            // break a block that just runs off its end would repeat forever --
            // but a block whose last statement leaves the block already has no
            // end to reach, and a valueless break there would type the label as
            // `()`.
            const bool endIsReachable = rv->valueNode
                || rv->nodes.empty()
                || !(cast<HIRExprNodeLoopControl>(rv->nodes.back().get()) || cast<HIRExprNodeReturn>(rv->nodes.back().get()));
            if (endIsReachable) {
                auto* breakNode = ctx.crate->pool->make<HIRExprNodeLoopControl>(v.span(), label, /*cont=*/false, ::std::move(rv->valueNode));
                rv->nodes.push_back(HIRExprNodeP(breakNode));
                rv->valueNode.reset();
            }
            auto* loop = ctx.crate->pool->make<HIRExprNodeLoop>(v.span(), label, HIRExprNodeP(rv));
            loop->requireLabel = true;
            this->rv.reset(loop);
        } else {
            this->rv.reset(static_cast<HIRExprNode*>(rv));
        }

        switch (v.blockType) {
            case ASTExprNodeBlock::Type::Bare:
                break;
            case ASTExprNodeBlock::Type::Unsafe:
                break;
            case ASTExprNodeBlock::Type::Const:
                this->rv.reset(ctx.crate->pool->make<HIRExprNodeConstBlock>(v.span(), std::move(this->rv)));
                break;
        }
    }

    virtual void visit(ASTExprNodeAsyncBlock& v) override {
        rv.reset(ctx.crate->pool->make<HIRExprNodeAsyncBlock>(v.span(), ctx.crate->types.infer(), lowerIsolated(v.inner), v.isMove, v.isUse));
    }

    virtual void visit(ASTExprNodeGeneratorBlock& v) override {
        auto origHasYield = hasYield;
        hasYield = false;
        auto inner = lowerIsolated(v.inner);
        hasYield = origHasYield;

        if (v.isAsync) {
            // An `async gen` block is an async block that yields: the body
            // returns nothing and each `yield` hands out an item.
            auto* node = ctx.crate->pool->make<HIRExprNodeAsyncBlock>(v.span(), ctx.crate->types.unit(), mv$(inner), v.isMove, false);
            node->isAsyncGen = true;
            node->yieldTy = ctx.LowerHIRType(v.returnType);
            rv.reset(node);
            return;
        }

        rv.reset(ctx.crate->pool->make<HIRExprNodeGenerator>(v.span(), ctx.LowerHIRType(v.returnType), ctx.crate->types.infer(), HIRPattern(), false, ctx.crate->types.infer(), mv$(inner), v.isMove, false, v.isCoroutineClosureBody));
        rv.reset(ctx.crate->pool->make<HIRExprNodeCallPath>(v.span(), HIRSimplePath(ctx.coreCrate, {"iter", "sources", "from_coroutine", "from_coroutine"}), makeVec1(mv$(rv))));
        rv.reset(ctx.crate->pool->make<HIRExprNodeCallMethod>(v.span(), mv$(rv), RcString::newInterned("fuse"), HIRPathParams(), ::std::vector<HIRExprNodeP>()));
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

        rv.reset(ctx.crate->pool->make<HIRExprNodeAsm>(v.span(), v.text, mv$(outputs), mv$(inputs), v.clobbers, v.flags));
    }

    virtual void visit(ASTExprNodeAsm2& v) override {
        std::vector<HIRExprNodeAsm2::Param> params;
        for (auto& p : v.params) {
            switch (p.tag()) {
                case ASTAsmParam::TAG_Const: {
                    auto& e = p.as_Const();
                    ASSERT_BUG(v.span(), e, "Missing node for ASM Const");
                    params.push_back(lower(e));
                    break;
                }
                case ASTAsmParam::TAG_Sym: {
                    auto& e = p.as_Sym();
                    params.push_back(ctx.LowerHIRPath(v.span(), e, FromASTPathClass::Value));
                    break;
                }
                case ASTAsmParam::TAG_Label: {
                    auto& e = p.as_Label();
                    params.push_back(HIRExprNodeAsm2::Param::make_Label({lower(e.code)}));
                    break;
                }
                case ASTAsmParam::TAG_RegSingle: {
                    auto& e = p.as_RegSingle();
                    params.push_back(
                        HIRExprNodeAsm2::Param::make_RegSingle({
                            e.dir,
                            e.spec.clone(),
                            e.val ? lower(e.val) : nullptr // e.g. `lateout(regname) _`
                        })
                    );
                    break;
                }
                case ASTAsmParam::TAG_Reg: {
                    auto& e = p.as_Reg();
                    params.push_back(HIRExprNodeAsm2::Param::make_Reg({e.dir, e.spec.clone(), e.valIn ? lower(e.valIn) : nullptr, e.valOut ? lower(e.valOut) : nullptr}));
                    break;
                }
            }
        }
        rv.reset(ctx.crate->pool->make<HIRExprNodeAsm2>(v.span(), v.options, v.lines, mv$(params)));
    }

    virtual void visit(ASTExprNodeFlow& v) override {
        switch (v.type) {
            case ASTExprNodeFlow::RETURN:
                if (v.value) {
                    rv.reset(ctx.crate->pool->make<HIRExprNodeReturn>(v.span(), lower(v.value)));
                } else {
                    rv.reset(ctx.crate->pool->make<HIRExprNodeReturn>(v.span(), HIRExprNodeP(ctx.crate->pool->make<HIRExprNodeTuple>(v.span(), ::std::vector<HIRExprNodeP>{}))));
                }
                break;
            case ASTExprNodeFlow::TAILCALL:
                if (!v.value) {
                    ERROR(v.span(), E0000, "`become` requires a call expression");
                }
                rv.reset(ctx.crate->pool->make<HIRExprNodeReturn>(v.span(), lower(v.value), true));
                break;
            case ASTExprNodeFlow::YIELD:
                hasYield = true;
                {
                    auto value = v.value ? lower(v.value) : HIRExprNodeP(ctx.crate->pool->make<HIRExprNodeTuple>(v.span(), ::std::vector<HIRExprNodeP>{}));
                    rv.reset(ctx.crate->pool->make<HIRExprNodeYield>(v.span(), std::move(value)));
                }
                break;
            case ASTExprNodeFlow::CONTINUE:
            case ASTExprNodeFlow::BREAK: {
                auto val = v.value ? lower(v.value) : HIRExprNodeP();
                ASSERT_BUG(v.span(), !(v.type == ASTExprNodeFlow::CONTINUE && val), "Continue with a value isn't allowed");
                auto target = resolveLoopLabel(v.span(), v.target);
                rv.reset(ctx.crate->pool->make<HIRExprNodeLoopControl>(v.span(), mv$(target), (v.type == ASTExprNodeFlow::CONTINUE), mv$(val)));
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
            auto pat = ctx.LowerHIRPattern(v.pat);
            auto type = ctx.LowerHIRType(v.type);
            auto nodeValue = lower(v.value);
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
                    for (size_t i = 0; i < pat.bindings.size(); i++) {
                        this->handleBinding(pat.bindings[i]);
                    }
                    // SplitSlice also defines bindings
                    if (auto* e = pat.data.opt_SplitSlice()) {
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
                        bindings.back().type = HIRPatternBinding::Type::Move;
                        it = mapping.insert(std::make_pair(pb.slot, newIdx)).first;
                    }
                    pb.isMutable = false;
                    pb.slot = it->second;
                }
            } visitor(ctx.crate->types, base, count);

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
                tupleVals.push_back(HIRExprNodeP(ctx.crate->pool->make<HIRExprNodeVariable>(v.span(), binding.name, slot)));
                newPats.push_back(HIRPattern(std::move(binding), HIRPattern::Data{}));
            }

            std::vector<HIRExprNodeMatch::Arm> matchArms(2);
            // `$pat => (a,b,c,...),`
            matchArms[0].patterns.push_back(std::move(pat));
            matchArms[0].code.reset(ctx.crate->pool->make<HIRExprNodeTuple>(v.span(), std::move(tupleVals)));
            matchArms[1].patterns.push_back(HIRPattern());
            // `_ => loop { let _: ! = $else; },
            matchArms[1].code.reset(ctx.crate->pool->make<HIRExprNodeLet>(v.span(), HIRPattern(), ctx.crate->types.diverge(), std::move(nodeElse)));
            matchArms[1].code.reset(ctx.crate->pool->make<HIRExprNodeLoop>(v.span(), "", std::move(matchArms[1].code), /*require_label*/ true));
            // HACK: Just use the code as-is.
            // `match $value: $ty {`
            auto matchValue = type->is_Infer() // Only emit the `: $ty` part if the type was specified (not a `_`)
                                  ? std::move(nodeValue)
                                  : HIRExprNodeP(ctx.crate->pool->make<HIRExprNodeUnsize>(v.span(), std::move(nodeValue), std::move(type)));
            auto match = HIRExprNodeP(ctx.crate->pool->make<HIRExprNodeMatch>(v.span(), std::move(matchValue), std::move(matchArms), true));

            // `let (a,b,c,...) = ...`
            rv.reset(ctx.crate->pool->make<HIRExprNodeLet>(v.span(), HIRPattern(::std::vector<HIRPatternBinding>(), HIRPattern::Data::make_Tuple({std::move(newPats)})), ctx.crate->types.infer(), std::move(match)));
        } else {
            rv.reset(ctx.crate->pool->make<HIRExprNodeLet>(v.span(), ctx.LowerHIRPattern(v.pat), ctx.LowerHIRType(v.type), lowerOpt(v.value), v.isSuper));
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

        rv.reset(ctx.crate->pool->make<HIRExprNodeAssign>(v.span(), H::getOp(v.op), lower(v.slot), lower(v.value)));
    }

    virtual void visit(ASTExprNodeBinOp& v) override {
        HIRExprNodeBinOp::Op op;
        switch (v.type) {
            case ASTExprNodeBinOp::RANGE: {
                BUG(v.span(), "Unexpected RANGE binop");
                break;
            }
            case ASTExprNodeBinOp::RANGE_INC: {
                BUG(v.span(), "Unexpected RANGE_INC binop");
                break;
            }
            case ASTExprNodeBinOp::PLACE_IN:
                rv.reset(ctx.crate->pool->make<HIRExprNodeEmplace>(v.span(), HIRExprNodeEmplace::Type::Placer, lower(v.left), lower(v.right)));
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

                rv.reset(ctx.crate->pool->make<HIRExprNodeBinOp>(v.span(), op, lower(v.left), lower(v.right)));
                break;
        }
    }

    virtual void visit(ASTExprNodeUniOp& v) override {
        HIRExprNodeUniOp::Op op;
        switch (v.type) {
            case ASTExprNodeUniOp::BOX: {
                rv.reset(ctx.crate->pool->make<HIRExprNodeEmplace>(v.span(), HIRExprNodeEmplace::Type::Boxer, HIRExprNodeP(ctx.crate->pool->make<HIRExprNodeTuple>(v.span(), ::std::vector<HIRExprNodeP>{})), lower(v.value)));
            } break;
            case ASTExprNodeUniOp::QMARK:
                BUG(v.span(), "Encounterd question mark operator (should have been expanded in AST)");
                break;

            case ASTExprNodeUniOp::REF:
                rv.reset(ctx.crate->pool->make<HIRExprNodeBorrow>(v.span(), HIRBorrowType::Shared, lower(v.value)));
                break;
            case ASTExprNodeUniOp::RawBorrow:
                rv.reset(ctx.crate->pool->make<HIRExprNodeRawBorrow>(v.span(), HIRBorrowType::Shared, lower(v.value)));
                break;
            case ASTExprNodeUniOp::REFMUT:
                rv.reset(ctx.crate->pool->make<HIRExprNodeBorrow>(v.span(), HIRBorrowType::Unique, lower(v.value)));
                break;
            case ASTExprNodeUniOp::RawBorrowMut:
                rv.reset(ctx.crate->pool->make<HIRExprNodeRawBorrow>(v.span(), HIRBorrowType::Unique, lower(v.value)));
                break;

            case ASTExprNodeUniOp::AWait:
                rv.reset(ctx.crate->pool->make<HIRExprNodeAWait>(v.span(), lower(v.value)));
                break;
            case ASTExprNodeUniOp::AWaitNext: {
                auto* node = ctx.crate->pool->make<HIRExprNodeAWait>(v.span(), lower(v.value));
                node->isNext = true;
                rv.reset(node);
                break;
            }
            case ASTExprNodeUniOp::USE:
                rv.reset(ctx.crate->pool->make<HIRExprNodeUse>(v.span(), lower(v.value)));
                break;

            case ASTExprNodeUniOp::INVERT:
                op = HIRExprNodeUniOp::Op::Invert;
                if (0) {
                    case ASTExprNodeUniOp::NEGATE:
                        op = HIRExprNodeUniOp::Op::Negate;
                        // `-0x8000_0000_0000_0000_0000_0000_0000_0000i128` is the
                        // minimum value, whose magnitude has no positive
                        // counterpart. Negating it at run time traps, so fold it
                        // into the literal -- the two's-complement pattern of the
                        // minimum is that magnitude.
                        if (const auto* lit = cast<ASTExprNodeInteger>(v.value.get())) {
                            unsigned bits = 0;
                            HIRCoreType type = HIRCoreType::I32;
                            switch (lit->datatype) {
                                case CORETYPE_I8: bits = 8; type = HIRCoreType::I8; break;
                                case CORETYPE_I16: bits = 16; type = HIRCoreType::I16; break;
                                case CORETYPE_I32: bits = 32; type = HIRCoreType::I32; break;
                                case CORETYPE_I64: bits = 64; type = HIRCoreType::I64; break;
                                case CORETYPE_I128: bits = 128; type = HIRCoreType::I128; break;
                                case CORETYPE_INT: bits = 64; type = HIRCoreType::Isize; break;
                                default: break;
                            }
                            if (bits != 0 && lit->value == (U128(1) << (bits - 1))) {
                                // A literal carries its value sign-extended to
                                // 128 bits, so a cast that widens it reads the
                                // sign rather than a positive magnitude.
                                rv.reset(ctx.crate->pool->make<HIRExprNodeLiteral>(v.span(), HIRExprNodeLiteral::Data::make_Integer({type, ~U128(0) << (bits - 1)})));
                                break;
                            }
                        }
                }
                rv.reset(ctx.crate->pool->make<HIRExprNodeUniOp>(v.span(), op, lower(v.value)));
                break;
        }
    }

    virtual void visit(ASTExprNodeCast& v) override {
        rv.reset(ctx.crate->pool->make<HIRExprNodeCast>(v.span(), lower(v.value), ctx.LowerHIRType(v.type)));
    }

    virtual void visit(ASTExprNodeTypeAnnotation& v) override {
        // TODO: Create a proper node for this
        // - Using `Unsize` works pretty well, but isn't quite "correct"
        rv.reset(ctx.crate->pool->make<HIRExprNodeUnsize>(v.span(), lower(v.value), ctx.LowerHIRType(v.type)));
    }

    virtual void visit(ASTExprNodeCallPath& v) override {
        ::std::vector<HIRExprNodeP> args;
        for (auto& arg : v.args) {
            args.push_back(lower(arg));
        }

        if (const auto* e = v.path.cls.opt_Local()) {
            rv.reset(ctx.crate->pool->make<HIRExprNodeCallValue>(v.span(), HIRExprNodeP(ctx.crate->pool->make<HIRExprNodeVariable>(v.span(), e->name, v.path.bindings.value.binding.as_Variable().slot)), mv$(args)));
        } else {
            switch (v.path.bindings.value.binding.tag()) {
default:
                rv.reset( ctx.crate->pool->make<HIRExprNodeCallPath>( v.span(),
                    ctx.LowerHIRPath(v.span(), v.path, FromASTPathClass::Value),
                    mv$( args )
                    ) );
                break;
                case ASTPathBindingValue::TAG_Static: {
                    auto& e = v.path.bindings.value.binding.as_Static();
                    bool isConst = e.static_ ? e.static_->sClass() == ASTStatic::Class::CONST : (e.hir ? false : true) // If HIR Pointer is null, this is a HIR::Const
                        ;
                    rv.reset(ctx.crate->pool->make<HIRExprNodeCallValue>(v.span(), HIRExprNodeP(ctx.crate->pool->make<HIRExprNodePathValue>(v.span(), ctx.LowerHIRPath(v.span(), v.path, FromASTPathClass::Value), isConst ? HIRExprNodePathValue::CONSTANT : HIRExprNodePathValue::STATIC)), mv$(args)));
                    break;
                }
                case ASTPathBindingValue::TAG_EnumVar: {
                    auto& e = v.path.bindings.value.binding.as_EnumVar();
                    ASSERT_BUG(v.span(), e.enum_ || e.hir, "Call path bound to an enum variant without its enum");
                    bool isUnit = false;
                    if (e.enum_) {
                        isUnit = e.enum_->variants().at(e.idx).data.is_Unit();
                    } else if (e.hir->data.is_Value()) {
                        isUnit = true;
                    } else {
                        isUnit = e.hir->data.as_Data().at(e.idx).type == ctx.crate->types.unit();
                    }
                    auto path = ctx.LowerHIRGenericPath(v.span(), v.path, FromASTPathClass::Value);
                    if (isUnit) {
                        auto value = HIRExprNodeP(ctx.crate->pool->make<HIRExprNodeUnitVariant>(v.span(), mv$(path), false));
                        rv.reset(ctx.crate->pool->make<HIRExprNodeCallValue>(v.span(), mv$(value), mv$(args)));
                    } else {
                        rv.reset(ctx.crate->pool->make<HIRExprNodeTupleVariant>(v.span(), mv$(path), false, mv$(args)));
                    }
                    break;
                }
                case ASTPathBindingValue::TAG_Struct: {
                    auto& e = v.path.bindings.value.binding.as_Struct();
                    ASSERT_BUG(v.span(), e.struct_ || e.hir, "Call path bound to a struct without its definition");
                    const bool isUnit = e.struct_ ? e.struct_->data.is_Unit() : e.hir->data.is_Unit();
                    auto path = ctx.LowerHIRGenericPath(v.span(), v.path, FromASTPathClass::Value);
                    if (isUnit) {
                        auto value = HIRExprNodeP(ctx.crate->pool->make<HIRExprNodeUnitVariant>(v.span(), mv$(path), true));
                        rv.reset(ctx.crate->pool->make<HIRExprNodeCallValue>(v.span(), mv$(value), mv$(args)));
                    } else {
                        rv.reset(ctx.crate->pool->make<HIRExprNodeTupleVariant>(v.span(), mv$(path), true, mv$(args)));
                    }
                    break;
                }
            }
        }
    }

    virtual void visit(ASTExprNodeCallMethod& v) override {
        ::std::vector<HIRExprNodeP> args;
        for (auto& arg : v.args) {
            args.push_back(lower(arg));
        }

        rv.reset(ctx.crate->pool->make<HIRExprNodeCallMethod>(v.span(), lower(v.val), v.method.name(), ctx.LowerHIRPathParams(v.span(), v.method.args(), /*allow_assoc=*/false), mv$(args)));
    }

    virtual void visit(ASTExprNodeCallObject& v) override {
        ::std::vector<HIRExprNodeP> args;
        for (auto& arg : v.args) {
            args.push_back(lower(arg));
        }

        rv.reset(ctx.crate->pool->make<HIRExprNodeCallValue>(v.span(), lower(v.val), mv$(args)));
    }

    virtual void visit(ASTExprNodeLoop& v) override {
        auto label = enterLoopLabel(v.label);
        auto code = lower(v.code);
        leaveLoopLabel(label);
        rv.reset(ctx.crate->pool->make<HIRExprNodeLoop>(v.span(), mv$(label), mv$(code)));
    }

    void visit(ASTExprNodeFor& v) override {
        // NOTE: This should already be desugared (as a pass before resolve)
        BUG(v.span(), "Encountered still-sugared for loop");
    }

    ::std::vector<HIRExprNodeMatch::Guard> ifletToGuards(std::vector<ASTIfLetCondition>& guards) {
        ::std::vector<HIRExprNodeMatch::Guard> rv;
        rv.reserve(guards.size());
        for (auto& c : guards) {
            auto condPat = c.optPat ? ctx.LowerHIRPattern(*c.optPat) : HIRPattern{HIRPatternBinding(), HIRPattern::Data::make_Value({HIRPattern::Value::make_Integer({HIRCoreType::Bool, U128(1)})})};
            auto condVal = lowerOpt(c.value);
            rv.push_back(HIRExprNodeMatch::Guard{std::move(condPat), std::move(condVal), c.optPat ? false : true});
        }
        return rv;
    }

    /// A node built by a desugaring rather than lowered from one in the source
    /// still needs a result type for type checking to fill in.
    template <typename T, typename... Args>
    HIRExprNodeP mkNode(Args&&... args) {
        auto* node = ctx.crate->pool->make<T>(::std::forward<Args>(args)...);
        node->resType = ctx.crate->types.infer();
        return HIRExprNodeP(node);
    }

    virtual void visit(ASTExprNodeWhile& v) override {
        // Desugar to `loop { match () { _ if ... => { body }, _ => break, } }`
        auto label = enterLoopLabel(v.label);
        ::std::vector<HIRExprNodeMatch::Arm> arms;
        arms.push_back(HIRExprNodeMatch::Arm{makeVec1(HIRPattern()), ifletToGuards(v.conditions), lower(v.code)});
        arms.push_back(HIRExprNodeMatch::Arm{makeVec1(HIRPattern()), {}, mkNode<HIRExprNodeLoopControl>(v.span(), "", false, nullptr)});
        leaveLoopLabel(label);
        rv.reset(ctx.crate->pool->make<HIRExprNodeLoop>(v.span(), mv$(label), mkNode<HIRExprNodeMatch>(v.span(), mkNode<HIRExprNodeTuple>(v.span(), ::std::vector<HIRExprNodeP>()), std::move(arms))));
    }

    virtual void visit(ASTExprNodeMatch& v) override {
        ::std::vector<HIRExprNodeMatch::Arm> arms;

        for (auto& arm : v.arms) {
            HIRExprNodeMatch::Arm newArm{{}, ifletToGuards(arm.guard), lower(arm.code)};

            for (const auto& pat : arm.patterns) {
                newArm.patterns.push_back(ctx.LowerHIRPattern(pat));
            }

            arms.push_back(mv$(newArm));
        }

        rv.reset(ctx.crate->pool->make<HIRExprNodeMatch>(v.span(), lower(v.val), mv$(arms)));
    }

    virtual void visit(ASTExprNodeIf& v) override {
        ::std::vector<HIRExprNodeMatch::Arm> arms;
        // Desugar to a `match`
        for (auto& arm : v.arms) {
            arms.push_back(HIRExprNodeMatch::Arm{makeVec1(HIRPattern()), ifletToGuards(arm.conditions), lower(arm.body)});
        }
        arms.push_back(HIRExprNodeMatch::Arm{makeVec1(HIRPattern()), {}, v.elseNode ? lower(v.elseNode) : mkNode<HIRExprNodeTuple>(v.span(), ::std::vector<HIRExprNodeP>())});

        rv.reset(ctx.crate->pool->make<HIRExprNodeMatch>(v.span(), mkNode<HIRExprNodeTuple>(v.span(), ::std::vector<HIRExprNodeP>()), std::move(arms)));
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

        if (v.datatype == CORETYPE_F16 || v.datatype == CORETYPE_F32 || v.datatype == CORETYPE_F64 || v.datatype == CORETYPE_F128) {
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
                case CORETYPE_F128:
                    type = HIRCoreType::F128;
                    break;
                default:
                    BUG(v.span(), "Unexpected floating point type");
            }
            const auto text = FMT(v.value);
            rv.reset(ctx.crate->pool->make<HIRExprNodeLiteral>(v.span(), HIRExprNodeLiteral::Data::make_Float({type, parseFloatValue(text.c_str())})));
            return;
        }
        rv.reset(ctx.crate->pool->make<HIRExprNodeLiteral>(v.span(), HIRExprNodeLiteral::Data::make_Integer({H::getType(v.span(), v.datatype), v.value})));
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
        rv.reset(ctx.crate->pool->make<HIRExprNodeLiteral>(v.span(), HIRExprNodeLiteral::Data::make_Float({ct, v.value})));
    }

    virtual void visit(ASTExprNodeBool& v) override {
        rv.reset(ctx.crate->pool->make<HIRExprNodeLiteral>(v.span(), HIRExprNodeLiteral::Data::make_Boolean(v.value)));
    }

    virtual void visit(ASTExprNodeString& v) override {
        rv.reset(ctx.crate->pool->make<HIRExprNodeLiteral>(v.span(), HIRExprNodeLiteral::Data::make_String(v.value)));
    }

    virtual void visit(ASTExprNodeByteString& v) override {
        ::std::vector<char> dat{v.value.begin(), v.value.end()};
        rv.reset(ctx.crate->pool->make<HIRExprNodeLiteral>(v.span(), HIRExprNodeLiteral::Data::make_ByteString(mv$(dat))));
    }

    virtual void visit(ASTExprNodeCString& v) override {
        rv.reset(ctx.crate->pool->make<HIRExprNodeLiteral>(v.span(), HIRExprNodeLiteral::Data::make_CString({v.value})));
    }

    virtual void visit(ASTExprNodeSuffixedLiteral& v) override {
        ERROR(v.span(), E0000, "Invalid suffix for literal `" << v.text << "`");
    }

    virtual void visit(ASTExprNodeClosure& v) override {
        HIRExprNodeClosure::argsT args;
        for (const auto& arg : v.args) {
            args.push_back(::std::make_pair(ctx.LowerHIRPattern(arg.first), ctx.LowerHIRType(arg.second)));
        }

        auto origHasYield = this->hasYield;
        this->hasYield = false;
        auto inner = lowerIsolated(v.code);
        auto hasYield = this->hasYield;
        this->hasYield = origHasYield;

        if (hasYield) {
            if (args.size() > 1) {
                ERROR(v.span(), E0000, "Coroutine closures take at most one resume argument.");
            }
            const bool hasResumePattern = !args.empty();
            auto resumeTy = hasResumePattern ? args.front().second : ctx.crate->types.unit();
            auto resumePattern = hasResumePattern ? ::std::move(args.front().first) : HIRPattern();
            auto* generator = ctx.crate->pool->make<HIRExprNodeGenerator>(v.span(), ctx.LowerHIRType(v.returnType), resumeTy, ::std::move(resumePattern), hasResumePattern, ctx.crate->types.infer(), mv$(inner), v.isMove, v.isPinned, false);
            generator->trackCaller = v.trackCaller;
            rv.reset(generator);
        } else {
            if (v.isPinned) {
                ERROR(v.span(), E0000, "Invalid use of `static` on non-yielding closure");
            }
            auto* closure = ctx.crate->pool->make<HIRExprNodeClosure>(v.span(), std::move(args), ctx.LowerHIRType(v.returnType), std::move(inner), v.isMove, v.isUse);
            closure->trackCaller = v.trackCaller;
            rv.reset(closure);
        }
    }

    virtual void visit(ASTExprNodeStructLiteral& v) override {
        if (v.path.bindings.type.binding.is_Union()) {
            if (v.values.size() != 1) {
                ERROR(v.span(), E0000, "Union constructors can only specify a single field");
            }
            if (v.baseValue) {
                ERROR(v.span(), E0000, "Union constructors can't take a base value");
            }
        }

        // `T { 0: a, 1: b }` names the fields of a tuple type by their index,
        // which is the tuple constructor written as a struct expression. The
        // path is bound by now, so which one it is can be read off it.
        if (!v.values.empty() && !v.baseValue) {
            bool allNumeric = true;
            for (const auto& val : v.values) {
                for (const char* c = val.name.c_str(); *c; c++) {
                    allNumeric &= (*c >= '0' && *c <= '9');
                }
            }
            bool isTuple = false;
            bool isStruct = false;
            // An alias is another name for the type, so look through it for
            // the constructor the expression names.
            const ASTPath* ctorPath = &v.path;
            {
                const ASTPath* p = &v.path;
                for (unsigned int i = 0; i < 32; i++) {
                    const auto* alias = p->bindings.type.binding.opt_TypeAlias();
                    if (!alias || !alias->alias_ || !alias->alias_->type_ || !alias->alias_->type_->isPath()) {
                        break;
                    }
                    p = &alias->alias_->type_->path();
                    ctorPath = p;
                }
            }
            if (const auto* binding = ctorPath->bindings.type.binding.opt_EnumVar()) {
                if (binding->enum_) {
                    isTuple = binding->enum_->variants().at(binding->idx).data.is_Tuple();
                } else if (binding->hir && binding->hir->data.is_Data()) {
                    const auto& var = binding->hir->data.as_Data().at(binding->idx);
                    isTuple = !var.isStruct && var.type != ctx.crate->types.unit();
                }
            } else if (const auto* binding = ctorPath->bindings.type.binding.opt_Struct()) {
                isStruct = true;
                if (binding->struct_) {
                    isTuple = binding->struct_->data.is_Tuple();
                } else if (binding->hir) {
                    isTuple = binding->hir->data.is_Tuple();
                }
            }
            if (allNumeric && isTuple) {
                ::std::vector<HIRExprNodeP> args;
                for (auto& val : v.values) {
                    args.push_back(lower(val.value));
                }
                rv.reset(ctx.crate->pool->make<HIRExprNodeTupleVariant>(v.span(), ctx.LowerHIRGenericPath(v.span(), *ctorPath, FromASTPathClass::Type), isStruct, mv$(args)));
                return;
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

            if (const auto* binding = v.path.bindings.type.binding.opt_EnumVar()) {
                EmptyKind kind = EmptyKind::None;
                if (binding->enum_) {
                    const auto& data = binding->enum_->variants().at(binding->idx).data;
                    kind = data.is_Unit() ? EmptyKind::Unit : data.is_Tuple() && data.as_Tuple().items.empty() ? EmptyKind::Tuple : EmptyKind::None;
                } else if (binding->hir) {
                    const auto& enm = *binding->hir;
                    if (enm.data.is_Value()) {
                        kind = EmptyKind::Unit;
                    } else {
                        const auto& var = enm.data.as_Data().at(binding->idx);
                        if (var.type == ctx.crate->types.unit()) {
                            kind = EmptyKind::Unit;
                        } else {
                            const auto& str = *var.type->as_Path().binding.as_Struct();
                            kind = str.data.is_Unit() ? EmptyKind::Unit : str.data.is_Tuple() && str.data.as_Tuple().empty() ? EmptyKind::Tuple : EmptyKind::None;
                        }
                    }
                }
                if (kind == EmptyKind::Unit) {
                    rv.reset(ctx.crate->pool->make<HIRExprNodeUnitVariant>(v.span(), ctx.LowerHIRGenericPath(v.span(), v.path, FromASTPathClass::Type), false));
                    return;
                }
                if (kind == EmptyKind::Tuple) {
                    rv.reset(ctx.crate->pool->make<HIRExprNodeTupleVariant>(v.span(), ctx.LowerHIRGenericPath(v.span(), v.path, FromASTPathClass::Type), false, ::std::vector<HIRExprNodeP>{}));
                    return;
                }
            } else if (const auto* binding = v.path.bindings.type.binding.opt_Struct()) {
                EmptyKind kind = EmptyKind::None;
                if (binding->struct_) {
                    const auto& data = binding->struct_->data;
                    kind = data.is_Unit() ? EmptyKind::Unit : data.is_Tuple() && data.as_Tuple().ents.empty() ? EmptyKind::Tuple : EmptyKind::None;
                } else if (binding->hir) {
                    const auto& data = binding->hir->data;
                    kind = data.is_Unit() ? EmptyKind::Unit : data.is_Tuple() && data.as_Tuple().empty() ? EmptyKind::Tuple : EmptyKind::None;
                }
                if (kind == EmptyKind::Unit) {
                    rv.reset(ctx.crate->pool->make<HIRExprNodeUnitVariant>(v.span(), ctx.LowerHIRGenericPath(v.span(), v.path, FromASTPathClass::Type), true));
                    return;
                }
                if (kind == EmptyKind::Tuple) {
                    rv.reset(ctx.crate->pool->make<HIRExprNodeTupleVariant>(v.span(), ctx.LowerHIRGenericPath(v.span(), v.path, FromASTPathClass::Type), true, ::std::vector<HIRExprNodeP>{}));
                    return;
                }
            }
        }
        auto ty = ctx.LowerHIRType(::mkType(*ctx.crate->pool, v.span(), v.path));
        if (v.path.bindings.type.binding.is_EnumVar()) {
            ASSERT_BUG(v.span(), ((*ty).is_Path() && ((*ty).as_Path().path.data.is_Generic())), "Enum variant path not GenericPath: " << ty);
            auto data = ty->cloneData();
            auto& gp = data.as_Path().path.data.as_Generic();
            auto varName = gp.path.popComponent();
            auto enumTy = ctx.crate->types.intern(mv$(data));
            ty = ctx.crate->types.path(HIRPath(enumTy, mv$(varName)), {});
        }
        rv.reset(ctx.crate->pool->make<HIRExprNodeStructLiteral>(v.span(), mv$(ty), !v.path.bindings.type.binding.is_EnumVar(), lowerOpt(v.baseValue), mv$(values)));
    }

    virtual void visit(ASTExprNodeStructLiteralPattern& v) override {
        if (v.path.bindings.type.binding.is_Union()) {
            if (v.values.size() != 1) {
                ERROR(v.span(), E0000, "Union constructors can only specify a single field");
            }
        }

        HIRExprNodeStructLiteral::tValues values;
        for (auto& val : v.values) {
            values.push_back(::std::make_pair(val.name, lower(val.value)));
        }
        auto ty = ctx.LowerHIRType(::mkType(*ctx.crate->pool, v.span(), v.path));
        if (v.path.bindings.type.binding.is_EnumVar()) {
            ASSERT_BUG(v.span(), ((*ty).is_Path() && ((*ty).as_Path().path.data.is_Generic())), "Enum variant path not GenericPath: " << ty);
            auto data = ty->cloneData();
            auto& gp = data.as_Path().path.data.as_Generic();
            auto varName = gp.path.popComponent();
            auto enumTy = ctx.crate->types.intern(mv$(data));
            ty = ctx.crate->types.path(HIRPath(enumTy, mv$(varName)), {});
        }
        rv.reset(ctx.crate->pool->make<HIRExprNodeStructLiteral>(v.span(), mv$(ty), !v.path.bindings.type.binding.is_EnumVar(), true, mv$(values)));
    }

    virtual void visit(ASTExprNodeArray& v) override {
        if (v.size) {
            // `[val; _]` takes its length from the expected type: the `_` is a
            // const-argument placeholder, not an expression.
            if (cast<const ASTExprNodeWildcardPattern>(&*v.size)) {
                rv.reset(ctx.crate->pool->make<HIRExprNodeArraySized>(
                    v.span(),
                    lower(v.values.at(0)),
                    HIRArraySize(HIRConstGeneric::make_Infer({}))
                ));
                return;
            }
            rv.reset(ctx.crate->pool->make<HIRExprNodeArraySized>(
                v.span(),
                lower(v.values.at(0)),
                // TODO: Should this size be a full expression on its own?
                lower(v.size)
            ));
        } else {
            ::std::vector<HIRExprNodeP> vals;
            for (auto& val : v.values) {
                vals.push_back(lower(val));
            }
            rv.reset(ctx.crate->pool->make<HIRExprNodeArrayList>(v.span(), mv$(vals)));
        }
    }

    virtual void visit(ASTExprNodeTuple& v) override {
        ::std::vector<HIRExprNodeP> vals;
        for (auto& val : v.values) {
            vals.push_back(lower(val));
        }
        rv.reset(ctx.crate->pool->make<HIRExprNodeTuple>(v.span(), mv$(vals)));
    }

    virtual void visit(ASTExprNodeNamedValue& v) override {
        if (const auto* e = v.path.cls.opt_Local()) {
            {
                auto& tuMatch = v.path.bindings.value.binding;
                switch (tuMatch.tag()) {
default:
                BUG(v.span(), "Named value was a local, but wasn't bound to a known type - " << v.path);
                    case ASTPathBindingValue::TAG_Generic: {
                        auto& binding = tuMatch.as_Generic();
                        rv.reset(ctx.crate->pool->make<HIRExprNodeConstParam>(v.span(), e->name, binding.index));
                        break;
                    }
                    case ASTPathBindingValue::TAG_Variable: {
                        auto& binding = tuMatch.as_Variable();
                        rv.reset(ctx.crate->pool->make<HIRExprNodeVariable>(v.span(), e->name, binding.slot));
                        break;
                    }
                }
            }
        } else {
            switch (v.path.bindings.value.binding.tag()) {
                case ASTPathBindingValue::TAG_Struct: {
                    auto& e = v.path.bindings.value.binding.as_Struct();
                    ASSERT_BUG(v.span(), e.struct_ || e.hir, "PathValue bound to a struct but pointer not set - " << v.path);
                    // Check the form and emit a PathValue if not a unit
                    bool isTupleConstructor = false;
                    if (e.struct_) {
                        if (e.struct_->data.is_Struct()) {
                            ERROR(v.span(), E0000, "Named value referring to a struct that isn't tuple-like or unit-like - " << v.path);
                        }
                        isTupleConstructor = e.struct_->data.is_Tuple();
                    } else {
                        const auto& str = *e.hir;
                        if (str.data.is_Unit()) {
                            isTupleConstructor = false;
                        } else if (str.data.is_Tuple()) {
                            isTupleConstructor = true;
                        } else {
                            ERROR(v.span(), E0000, "Named value referring to a struct that isn't tuple-like or unit-like - " << v.path);
                        }
                    }
                    if (isTupleConstructor) {
                        rv.reset(ctx.crate->pool->make<HIRExprNodePathValue>(v.span(), ctx.LowerHIRPath(v.span(), v.path, FromASTPathClass::Value), HIRExprNodePathValue::STRUCT_CONSTR));
                    } else {
                        rv.reset(ctx.crate->pool->make<HIRExprNodeUnitVariant>(v.span(), ctx.LowerHIRGenericPath(v.span(), v.path, FromASTPathClass::Value), true));
                    }
                    break;
                }
                case ASTPathBindingValue::TAG_EnumVar: {
                    auto& e = v.path.bindings.value.binding.as_EnumVar();
                    ASSERT_BUG(v.span(), e.enum_ || e.hir, "PathValue bound to an enum but pointer not set - " << v.path);
                    const auto& varName = v.path.nodes().back().name();
                    bool isTupleConstructor = false;
                    unsigned int varIdx;
                    if (e.enum_) {
                        const auto& enm = *e.enum_;
                        auto it = ::std::find_if(enm.variants().begin(), enm.variants().end(), [&](const auto& x) {
                            return x.name == varName;
                        });
                        assert(it != enm.variants().end());

                        varIdx = static_cast<unsigned int>(it - enm.variants().begin());
                        if (it->data.is_Struct()) {
                            ERROR(v.span(), E0000, "Named value referring to an enum that isn't tuple-like or unit-like - " << v.path);
                        }
                        isTupleConstructor = it->data.is_Tuple() && it->data.as_Tuple().items.size() > 0;
                    } else {
                        const auto& enm = *e.hir;
                        auto idx = enm.findVariant(varName);
                        assert(idx != SIZE_MAX);

                        varIdx = idx;
                        if (const auto* ee = enm.data.opt_Data()) {
                            if (ee->at(idx).type == ctx.crate->types.unit()) {
                            }
                            // TODO: Assert that it's not a struct-like
                            else {
                                isTupleConstructor = true;
                            }
                        }
                    }
                    (void)varIdx; // TODO: Save time later by saving this.
                    if (isTupleConstructor) {
                        rv.reset(ctx.crate->pool->make<HIRExprNodePathValue>(v.span(), ctx.LowerHIRPath(v.span(), v.path, FromASTPathClass::Value), HIRExprNodePathValue::ENUM_VAR_CONSTR));
                    } else {
                        rv.reset(ctx.crate->pool->make<HIRExprNodeUnitVariant>(v.span(), ctx.LowerHIRGenericPath(v.span(), v.path, FromASTPathClass::Value), false));
                    }
                    break;
                }
                case ASTPathBindingValue::TAG_Function: {
                    auto& e = v.path.bindings.value.binding.as_Function();
                    (void)e;
                    rv.reset(ctx.crate->pool->make<HIRExprNodePathValue>(v.span(), ctx.LowerHIRPath(v.span(), v.path, FromASTPathClass::Value), HIRExprNodePathValue::FUNCTION));
                    break;
                }
                case ASTPathBindingValue::TAG_Static: {
                    auto& e = v.path.bindings.value.binding.as_Static();
                    if (e.static_) {
                        if (e.static_->sClass() != ASTStatic::CONST) {
                            rv.reset(ctx.crate->pool->make<HIRExprNodePathValue>(v.span(), ctx.LowerHIRPath(v.span(), v.path, FromASTPathClass::Value), HIRExprNodePathValue::STATIC));
                        } else {
                            rv.reset(ctx.crate->pool->make<HIRExprNodePathValue>(v.span(), ctx.LowerHIRPath(v.span(), v.path, FromASTPathClass::Value), HIRExprNodePathValue::CONSTANT));
                        }
                    } else if (e.hir) {
                        rv.reset(ctx.crate->pool->make<HIRExprNodePathValue>(v.span(), ctx.LowerHIRPath(v.span(), v.path, FromASTPathClass::Value), HIRExprNodePathValue::STATIC));
                    }
                    // HACK: If the HIR pointer is nullptr, then it refers to a `const
                    else {
                        rv.reset(ctx.crate->pool->make<HIRExprNodePathValue>(v.span(), ctx.LowerHIRPath(v.span(), v.path, FromASTPathClass::Value), HIRExprNodePathValue::CONSTANT));
                    }
                    break;
                }
break;
                default:
                    auto p = ctx.LowerHIRPath(v.span(), v.path, FromASTPathClass::Value);
                    ASSERT_BUG(v.span(), !p.data.is_Generic(), "Unknown binding for PathValue but path is generic - " << v.path);
                    rv.reset(ctx.crate->pool->make<HIRExprNodePathValue>(v.span(), mv$(p), HIRExprNodePathValue::UNKNOWN));
            }
        }
    }

    virtual void visit(ASTExprNodeField& v) override {
        rv.reset(ctx.crate->pool->make<HIRExprNodeField>(v.span(), lower(v.obj), v.name));
    }

    virtual void visit(ASTExprNodeIndex& v) override {
        rv.reset(ctx.crate->pool->make<HIRExprNodeIndex>(v.span(), lower(v.obj), lower(v.idx)));
    }

    virtual void visit(ASTExprNodeDeref& v) override {
        rv.reset(ctx.crate->pool->make<HIRExprNodeDeref>(v.span(), lower(v.value)));
    }
};

HIRExprPtr AST2HIR::LowerHIRExprNode(const ASTExprNode& e) {
    LowerHIRExprNodeVisitor v(*this);

    const_cast<ASTExprNode*>(&e)->visit(v);

    if (!v.rv) {
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
    } initialise(crate->types);

    initialise.visitNodePtr(v.rv);

    return HIRExprPtr(mv$(v.rv));
}
