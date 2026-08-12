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

::HIR::ExprPtr LowerHIR_Expr(const ::AST::Expr& e);
::HIR::Module LowerHIR_Module(const ::AST::Module& module, ::HIR::ItemPath path, ::std::vector<::HIR::SimplePath> traits = {});
::HIR::Function LowerHIR_Function(::HIR::ItemPath path, const ::AST::AttributeList& attrs, const ::AST::Function& f, const ::HIR::TypeData* self_type);
::HIR::ValueItem LowerHIR_Static(::HIR::ItemPath p, const ::AST::AttributeList& attrs, const ::AST::Static& e, const Span& sp, const RcString& name);
::HIR::PathParams LowerHIR_PathParams(const Span& sp, const ::AST::PathParams& src_params, bool allow_assoc);
::HIR::ConstGeneric LowerHIR_ConstGeneric(const ::AST::ExprNode& node_ref);
::HIR::TraitPath LowerHIR_TraitPath(const Span& sp, const ::AST::Path& path, const AST::HigherRankedBounds& hrbs, bool allow_bounds = false, AST::BoundConstness constness = AST::BoundConstness::Never);
::HIR::GenericParams LowerHIR_HigherRankedBounds(const AST::HigherRankedBounds& hrbs);

::HIR::SimplePath path_Sized;
::HIR::SimplePath path_PointeeSized;
::HIR::SimplePath path_MetadataSized;
RcString g_core_crate;
RcString g_crate_name;
::HIR::Crate* g_crate_ptr = nullptr;
const ::AST::Crate* g_ast_crate_ptr;

namespace {
    ::HIR::BoundConstness LowerHIR_BoundConstness(::AST::BoundConstness v) {
        switch (v) {
        case ::AST::BoundConstness::Never: return ::HIR::BoundConstness::Never;
        case ::AST::BoundConstness::Always: return ::HIR::BoundConstness::Always;
        case ::AST::BoundConstness::Maybe: return ::HIR::BoundConstness::Maybe;
        }
        throw "Invalid bound constness";
    }
}

// --------------------------------------------------------------------
HIR::LifetimeRef LowerHIR_LifetimeRef(const ::AST::LifetimeRef& r) {
    assert(r.binding() >= 0xFFF0 || r.binding() < 1024);
    return HIR::LifetimeRef(
        // TODO: names?
        r.binding()
    );
}

::HIR::Publicity LowerHIR_Vis(const ::HIR::SimplePath& mod_path, const AST::Visibility& vis) {
    if (vis.is_global()) {
        return ::HIR::Publicity::new_global();
    }
    const auto* ap = &vis.vis_path();
    return ::HIR::Publicity::new_priv(::HIR::SimplePath((ap->crate == "" ? g_crate_name : ap->crate), ap->nodes));
}

::HIR::GenericParams LowerHIR_GenericParams(const ::AST::GenericParams& gp, bool* self_is_sized) {
    ::HIR::GenericParams rv;

    for (const auto& param : gp.m_params) {
        TU_MATCH_HDRA( (param), {)
        TU_ARMA(None, _) {
            }
            TU_ARMA(Lifetime, lft_def) {
                rv.m_lifetimes.push_back(HIR::LifetimeDef{lft_def.name().name});
            }
            TU_ARMA(Type, tp) {
                rv.m_types.push_back({tp.name(), LowerHIR_Type(tp.get_default()), true});
            }
            TU_ARMA(Value, tp) {
                rv.m_values.push_back(HIR::ValueParamDef{tp.name().name, LowerHIR_Type(tp.type()), tp.default_value() ? LowerHIR_ConstGeneric(tp.default_value().node()) : ::HIR::ConstGeneric::make_Infer({})});
            }
        }
    }

    for (const auto& bound : gp.m_bounds) {
        TU_MATCH_HDRA( (bound), {)
        TU_ARMA(None, e) {
            }
            TU_ARMA(Lifetime, e) {
                rv.m_bounds.push_back(::HIR::GenericBound::make_Lifetime({LowerHIR_LifetimeRef(e.test), LowerHIR_LifetimeRef(e.bound)}));
            }
            TU_ARMA(TypeLifetime, e) {
                rv.m_bounds.push_back(::HIR::GenericBound::make_TypeLifetime({LowerHIR_Type(e.type), LowerHIR_LifetimeRef(e.bound)}));
            }
            TU_ARMA(IsTrait, e) {
                //const auto& sp = e.span;
                auto type = LowerHIR_Type(e.type);

                // TODO: Check if this trait is `Sized` and ignore if it is? (It's a useless bound)

                if (!e.outer_hrbs.empty() && !e.inner_hrbs.empty()) {
                    // NOTE: rustc doesn't allow this (E0316)
                    TODO(bound.span, "Handle two layers of HRBs in a bound");
                }

                auto bound_trait_path = LowerHIR_TraitPath(bound.span, e.trait, e.inner_hrbs, /*allow_bounds=*/true, e.constness);
                auto tp_bounds = mv$(bound_trait_path.m_trait_bounds);
                bound_trait_path.m_trait_bounds.clear();

                // 1.90 added some traits that imply ?Sized
                if (bound_trait_path.m_path.m_path == path_PointeeSized || bound_trait_path.m_path.m_path == path_MetadataSized) {
                    if (const auto* ge = type->opt_Generic()) {
                        if (ge->binding == GENERIC_Self) {
                            *self_is_sized = false;
                        } else {
                            auto idx = ge->idx();
                            ASSERT_BUG(bound.span, idx < rv.m_types.size(), "Bounded type out of bounds: " << ge->binding << " " << type);
                            rv.m_types[idx].m_is_sized = false;
                        }
                    }
                }

                rv.m_bounds.push_back(::HIR::GenericBound::make_TraitBound({box$(LowerHIR_HigherRankedBounds(e.outer_hrbs)), type, mv$(bound_trait_path), LowerHIR_BoundConstness(e.constness)}));

                for (auto& bound : tp_bounds) {
                    const auto& name = bound.first;
                    const auto& src_trait = bound.second.source_trait;
                    const auto& params = bound.second.aty_params;
                    for (auto& trait : bound.second.traits) {
                        std::unique_ptr<HIR::GenericParams> hrls;
                        if (!e.outer_hrbs.empty()) {
                            hrls = box$(LowerHIR_HigherRankedBounds(e.outer_hrbs));
                        }
                        if (!e.inner_hrbs.empty()) {
                            hrls = box$(LowerHIR_HigherRankedBounds(e.inner_hrbs));
                        }
                        rv.m_bounds.push_back(::HIR::GenericBound::make_TraitBound({std::move(hrls), g_crate_ptr->m_types.path(::HIR::Path(type, src_trait.clone(), name, params.clone()), {}), std::move(trait)}));
                    }
                    bound.second.traits.clear();
                }
            }
            TU_ARMA(MaybeTrait, e) {
                auto type = LowerHIR_Type(e.type);
                if (!type->is_Generic()) {
                    BUG(bound.span, "MaybeTrait on non-param - " << type);
                }
                const auto& ge = type->as_Generic();
                unsigned param_idx;
                if (ge.binding == 0xFFFF) {
                    if (!self_is_sized) {
                        BUG(bound.span, "MaybeTrait on parameter on Self when not allowed");
                    }
                    param_idx = 0xFFFF;
                } else {
                    param_idx = ge.idx();
                    if (param_idx >= rv.m_types.size()) {
                        BUG(bound.span, "MaybeTrait on parameter not in parameter list (#" << ge.binding << ")");
                    }
                }

                // Compare with list of known default traits (just Sized atm) and set a marker
                auto trait = LowerHIR_GenericPath(bound.span, e.trait, FromAST_PathClass::Type);
                if (trait.m_path == path_Sized) {
                    if (param_idx == 0xFFFF) {
                        assert(self_is_sized);
                        *self_is_sized = false;
                    } else {
                        assert(param_idx < rv.m_types.size());
                        rv.m_types[param_idx].m_is_sized = false;
                    }
                } else {
                    ERROR(bound.span, E0000, "MaybeTrait on unknown trait " << trait.m_path);
                }
            }
            TU_ARMA(NotTrait, e) {
                TODO(bound.span, "Negative trait bounds");
            }
            TU_ARMA(Equality, e) {
                rv.m_bounds.push_back(::HIR::GenericBound::make_TypeEquality({LowerHIR_Type(e.type), LowerHIR_Type(e.replacement)}));
            }
        }
    }

    return rv;
}

::HIR::Path LowerHIR_Pattern_Path(const Span& sp, const ::AST::Path& path, FromAST_PathClass pc) {
    if (const auto* be = path.m_bindings.type.binding.opt_TypeParameter()) {
        if (be->slot == GENERIC_Self) {
            // HACK: Return `<Self>::` (to be expanded later on)
            return ::HIR::Path(g_crate_ptr->m_types.self(), "");
        }
    }
    return LowerHIR_Path(sp, path, pc);
}

namespace {
    ::HIR::PatternBinding::Type convert_binding_type(::AST::PatternBinding::Type pbt) {
        switch (pbt) {
            case ::AST::PatternBinding::Type::MOVE:
                return ::HIR::PatternBinding::Type::Move;
            case ::AST::PatternBinding::Type::REF:
                return ::HIR::PatternBinding::Type::Ref;
            case ::AST::PatternBinding::Type::MUTREF:
                return ::HIR::PatternBinding::Type::MutRef;
        }
        throw "";
    }
}

::HIR::Pattern LowerHIR_Pattern(const ::AST::Pattern& pat) {
    TRACE_FUNCTION_F("@" << pat.span() << " pat = " << pat);

    std::vector<::HIR::PatternBinding> bindings;
    for (const auto& pb : pat.bindings()) {
        bindings.push_back(::HIR::PatternBinding(pb.m_mutable, convert_binding_type(pb.m_type), pb.m_name.name, pb.m_slot));
    }

    struct H {
        static ::std::vector<::HIR::Pattern> lowerhir_patternvec(const ::std::vector<::AST::Pattern>& sub_patterns) {
            ::std::vector<::HIR::Pattern> rv;
            for (const auto& sp : sub_patterns) {
                rv.push_back(LowerHIR_Pattern(sp));
            }
            return rv;
        }

        static ::HIR::CoreType get_int_type(const Span& sp, const ::eCoreType ct) {
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

        static ::HIR::CoreType get_float_type(const Span& sp, const ::eCoreType ct) {
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

        static ::HIR::Pattern::Value lowerhir_pattern_value(const Span& sp, const ::AST::Pattern::Value& v) {
            TU_MATCH_HDRA((v), {)
            TU_ARMA(Invalid, e) {
                    BUG(sp, "Encountered Invalid value in Pattern");
                }
                TU_ARMA(Integer, e) {
                    return ::HIR::Pattern::Value::make_Integer({H::get_int_type(sp, e.type), e.value});
                }
                TU_ARMA(Float, e) {
                    return ::HIR::Pattern::Value::make_Float({H::get_float_type(sp, e.type), e.value});
                }
                TU_ARMA(String, e) {
                    return ::HIR::Pattern::Value::make_String(e);
                }
                TU_ARMA(ByteString, e) {
                    return ::HIR::Pattern::Value::make_ByteString({e.v});
                }
                TU_ARMA(Named, e) {
                    return ::HIR::Pattern::Value::make_Named({LowerHIR_Pattern_Path(sp, e, FromAST_PathClass::Value), nullptr});
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
        return ::HIR::Pattern{mv$(bindings), ::HIR::Pattern::Data::make_Box({box$(LowerHIR_Pattern(*e.sub))})};
        TU_ARMA(Ref, e)
        return ::HIR::Pattern{mv$(bindings), ::HIR::Pattern::Data::make_Ref({(e.mut ? ::HIR::BorrowType::Unique : ::HIR::BorrowType::Shared), box$(LowerHIR_Pattern(*e.sub))})};
        TU_ARMA(Tuple, e) {
            auto leading = H::lowerhir_patternvec(e.start);
            auto trailing = H::lowerhir_patternvec(e.end);

            if (e.has_wildcard) {
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
            auto leading = H::lowerhir_patternvec(e.tup_pat.start);
            auto trailing = H::lowerhir_patternvec(e.tup_pat.end);

            if (!e.tup_pat.has_wildcard) {
                assert(trailing.size() == 0);
            }

            return ::HIR::Pattern(
                mv$(bindings),
                ::HIR::Pattern::Data::make_PathTuple({
                    LowerHIR_Pattern_Path(pat.span(), e.path, FromAST_PathClass::Value),
                    ::HIR::Pattern::PathBinding(),
                    mv$(leading),
                    e.tup_pat.has_wildcard,
                    mv$(trailing),
                    0 // Total size unknown still
                })
            );
        }
        ///
        /// Struct pattern
        ///
        TU_ARMA(Struct, e) {
            ::std::vector<::std::pair<RcString, ::HIR::Pattern>> sub_patterns;
            for (const auto& sp : e.sub_patterns) {
                sub_patterns.push_back(::std::make_pair(sp.name, LowerHIR_Pattern(sp.pat)));
            }

            // No sub-patterns, no `..`, and the VALUE binding points to an enum variant
            if (e.sub_patterns.empty() /*&& !e.is_exhaustive*/) {
                if (/*const auto* pbp =*/e.path.m_bindings.value.binding.opt_EnumVar()) {
                    return ::HIR::Pattern{
                        mv$(bindings),
                        ::HIR::Pattern::Data::make_PathNamed(
                            {LowerHIR_GenericPath(pat.span(), e.path, FromAST_PathClass::Value),
                             //::HIR::Pattern::PathBinding::make_Enum({ pbp->hir, pbp->idx }),
                             ::HIR::Pattern::PathBinding(),
                             mv$(sub_patterns),
                             e.is_exhaustive}
                        )
                    };
                }
            }

            return ::HIR::Pattern(mv$(bindings), ::HIR::Pattern::Data::make_PathNamed({LowerHIR_Pattern_Path(pat.span(), e.path, FromAST_PathClass::Type), ::HIR::Pattern::PathBinding(), mv$(sub_patterns), e.is_exhaustive}));
        }

        TU_ARMA(Value, e) {
            if (e.end.is_Invalid()) {
                return ::HIR::Pattern{mv$(bindings), ::HIR::Pattern::Data::make_Value({H::lowerhir_pattern_value(pat.span(), e.start)})};
            } else if (e.start.is_Invalid()) {
                return ::HIR::Pattern{mv$(bindings), ::HIR::Pattern::Data::make_Range({{}, box$(H::lowerhir_pattern_value(pat.span(), e.end)), true})};
            } else {
                return ::HIR::Pattern{mv$(bindings), ::HIR::Pattern::Data::make_Range({box$(H::lowerhir_pattern_value(pat.span(), e.start)), box$(H::lowerhir_pattern_value(pat.span(), e.end)), true})};
            }
        }
        TU_ARMA(ValueLeftInc, e) {
            if (e.end.is_Invalid()) {
                return ::HIR::Pattern{mv$(bindings), ::HIR::Pattern::Data::make_Range({box$(H::lowerhir_pattern_value(pat.span(), e.start)), {}, false})};
            }
            if (e.start.is_Invalid()) {
                return ::HIR::Pattern{mv$(bindings), ::HIR::Pattern::Data::make_Range({{}, box$(H::lowerhir_pattern_value(pat.span(), e.end)), false})};
            }
            return ::HIR::Pattern{mv$(bindings), ::HIR::Pattern::Data::make_Range({box$(H::lowerhir_pattern_value(pat.span(), e.start)), box$(H::lowerhir_pattern_value(pat.span(), e.end)), false})};
        }
        TU_ARMA(Slice, e) {
            ::std::vector<::HIR::Pattern> leading;
            for (const auto& sp : e.sub_pats) {
                leading.push_back(LowerHIR_Pattern(sp));
            }
            return ::HIR::Pattern{mv$(bindings), ::HIR::Pattern::Data::make_Slice({mv$(leading)})};
        }
        TU_ARMA(SplitSlice, e) {
            ::std::vector<::HIR::Pattern> leading;
            for (const auto& sp : e.leading) {
                leading.push_back(LowerHIR_Pattern(sp));
            }

            ::std::vector<::HIR::Pattern> trailing;
            for (const auto& sp : e.trailing) {
                trailing.push_back(LowerHIR_Pattern(sp));
            }

            auto extra_bind = e.extra_bind.is_valid() ? ::HIR::PatternBinding(false, convert_binding_type(e.extra_bind.m_type), e.extra_bind.m_name.name, e.extra_bind.m_slot) : ::HIR::PatternBinding();

            return ::HIR::Pattern{mv$(bindings), ::HIR::Pattern::Data::make_SplitSlice({mv$(leading), mv$(extra_bind), mv$(trailing)})};
        }
        TU_ARMA(Or, e) {
            ::std::vector<::HIR::Pattern> subpats;
            for (const auto& sp : e) {
                subpats.push_back(LowerHIR_Pattern(sp));
            }
            return ::HIR::Pattern{mv$(bindings), ::HIR::Pattern::Data::make_Or(mv$(subpats))};
        }
    }
    throw "unreachable";
}

::HIR::ExprPtr LowerHIR_Expr(const ::std::shared_ptr<::AST::ExprNode>& e) {
    if (e.get()) {
        return LowerHIR_ExprNode(*e);
    } else {
        return ::HIR::ExprPtr();
    }
}

::HIR::ExprPtr LowerHIR_Expr(const ::AST::Expr& e) {
    if (e.is_valid()) {
        return LowerHIR_ExprNode(e.node());
    } else {
        return ::HIR::ExprPtr();
    }
}

::HIR::SimplePath LowerHIR_SimplePath(const Span& sp, const ::AST::Path& path, FromAST_PathClass pc, bool allow_final_generic) {
    if (!allow_final_generic) {
        ASSERT_BUG(sp, path.m_class.is_Absolute(), "Encountered non-Absolute path when creating ::HIR::SimplePath");
        if (path.m_class.as_Absolute().nodes.size() > 0) {
            ASSERT_BUG(sp, path.m_class.as_Absolute().nodes.back().args().is_empty(), "Encountered path with parameters when creating ::HIR::SimplePath");
        }
    } else {
        ASSERT_BUG(sp, path.m_class.is_Absolute(), "Encountered non-Absolute path when creating ::HIR::GenericPath");
    }

    const AST::AbsolutePath* ap = nullptr;
    switch (pc) {
        case FromAST_PathClass::Value:
            ASSERT_BUG(sp, !path.m_bindings.value.is_Unbound(), "Encountered unbound value path - " << path);
            ap = &path.m_bindings.value.path;
            break;
        case FromAST_PathClass::Type:
            ASSERT_BUG(sp, !path.m_bindings.type.is_Unbound(), "Encountered unbound type path - " << path);
            ap = &path.m_bindings.type.path;
            break;
        case FromAST_PathClass::Macro:
            ASSERT_BUG(sp, !path.m_bindings.macro.is_Unbound(), "Encountered unbound macro path - " << path);
            ap = &path.m_bindings.macro.path;
            break;
    }
    assert(ap);
    return ::HIR::SimplePath((ap->crate == "" ? g_crate_name : ap->crate), ap->nodes);
}

::HIR::PathParams LowerHIR_PathParams(const Span& sp, const ::AST::PathParams& src_params, bool allow_assoc) {
    ::HIR::PathParams params;

    size_t num_lft = 0;
    size_t num_ty = 0;
    size_t num_val = 0;

    for (const auto& param : src_params.m_entries) {
        TU_MATCH_HDRA( (param), {)
        TU_ARMA(Null, ty) {
            }
            TU_ARMA(Lifetime, lft) {
                num_lft++;
            }
            TU_ARMA(Type, ty) {
                num_ty++;
            }
            TU_ARMA(Value, iv) {
                num_val++;
            }
            TU_ARMA(AssociatedTyEqual, ty) {
            }
            TU_ARMA(AssociatedTyBound, ty) {
            }
        }
    }

    params.m_lifetimes.reserve_init(num_lft);
    params.m_types.reserve_init(num_ty);
    params.m_values.reserve_init(num_val);
    for (const auto& param : src_params.m_entries) {
        TU_MATCH_HDRA( (param), {)
        TU_ARMA(Null, ty) {
            }
            TU_ARMA(Lifetime, lft) {
                params.m_lifetimes.push_back(LowerHIR_LifetimeRef(lft));
            }
            TU_ARMA(Type, ty) {
                params.m_types.push_back(LowerHIR_Type(ty));
            }
            TU_ARMA(Value, iv) {
                ASSERT_BUG(sp, iv, "Value parameter with null node");
                params.m_values.push_back(LowerHIR_ConstGeneric(*iv));
            }
            TU_ARMA(AssociatedTyEqual, ty) {
                if (!allow_assoc) {
                    BUG(sp, "Encountered path parameters with associated type bounds where they are not allowed");
                }
            }
            TU_ARMA(AssociatedTyBound, ty) {
                if (!allow_assoc) {
                    BUG(sp, "Encountered path parameters with associated type bounds where they are not allowed");
                }
            }
        }
    }

    return params;
}

::HIR::ConstGeneric LowerHIR_ConstGeneric(const ::AST::ExprNode& node_ref) {
    const Span& sp = node_ref.span();
    const ::AST::ExprNode* node_p = &node_ref;
    if (const auto* e = cast<const AST::ExprNode_Block>(node_p)) {
        if (e->m_nodes.size() == 1 && !e->m_nodes.back().has_semicolon) {
            node_p = e->m_nodes.back().node.get();
        }
    }
    // TODO: Explicitly handle each expected variant... or add a proper consteval expression
    if (const auto* e = cast<const AST::ExprNode_NamedValue>(node_p)) {
        if (e->m_path.is_trivial()) {
            const auto& b = e->m_path.m_bindings.value.binding;
            ASSERT_BUG(sp, b.is_Generic(), "Trivial path not type parameter - " << e->m_path << " - " << b.tag_str());
            const auto& param = b.as_Generic();
            return HIR::GenericRef(e->m_path.as_trivial(), param.index);
        }
    }
    return std::make_unique<HIR::ConstGeneric_Unevaluated>(LowerHIR_ExprNode(node_ref));
}

::HIR::GenericPath LowerHIR_GenericPath(const Span& sp, const ::AST::Path& path, FromAST_PathClass pc, bool allow_assoc) {
    if (const auto* e = path.m_class.opt_Absolute()) {
        auto simpepath = LowerHIR_SimplePath(sp, path, pc, /*allow_params*/ true);
        ::HIR::PathParams params = LowerHIR_PathParams(sp, e->nodes.back().args(), allow_assoc);
        auto rv = ::HIR::GenericPath(mv$(simpepath), mv$(params));
        DEBUG(path << " => " << rv);
        return rv;
    } else {
        if (const auto* e = path.m_class.opt_UFCS()) {
            DEBUG(path);
            if (!e->type) {
            }
            //else if( e->trait ) {
            //}
            else if (!e->nodes.empty()) {
            } else if (!e->type->m_data.is_Path()) {
            } else {
                // HACK: `Self` replacement
                ASSERT_BUG(sp, pc == FromAST_PathClass::Type, "`Self` used in value context");
                return LowerHIR_GenericPath(sp, *e->type->m_data.as_Path(), pc, false);
            }
        }

        BUG(sp, "Encountered non-Absolute path when creating ::HIR::GenericPath - " << path);
    }
}

::HIR::GenericParams LowerHIR_HigherRankedBounds(const AST::HigherRankedBounds& hrbs) {
    HIR::GenericParams params;
    for (const auto& lft_def : hrbs.m_lifetimes) {
        params.m_lifetimes.push_back(HIR::LifetimeDef{lft_def.name().name});
    }
    return params;
}

::HIR::TraitPath LowerHIR_TraitPath(const Span& sp, const ::AST::Path& path, const AST::HigherRankedBounds& hrbs, bool ignore_bounds /*=false*/, AST::BoundConstness constness /*=Never*/) {
    DEBUG(hrbs << " " << path);
    ::HIR::TraitPath rv{
        hrbs.empty() ? nullptr : box$(LowerHIR_HigherRankedBounds(hrbs)), // m_hrtbs
        LowerHIR_GenericPath(sp, path, FromAST_PathClass::Type, /*allow_assoc=*/true),
        {},
        {},
        nullptr,
        LowerHIR_BoundConstness(constness)
    };
    // Parenthesised Fn-trait syntax follows function lifetime-elision rules.
    if (!rv.m_hrtbs && path.nodes().back().args().m_is_paren) {
        HIR::GenericParams params;
        rv.m_hrtbs = box$(params);
    }
    if (rv.m_hrtbs && path.nodes().back().args().m_is_paren) {
        rv.m_lifetime_elision = true;
    }

    if (rv.m_hrtbs) {
        DEBUG("HRLS = " << rv.m_hrtbs->fmt_args());
    } else {
        DEBUG("No HRLS");
    }

    struct H {
        static ::HIR::GenericPath find_source_trait_hir(const Span& sp, const ::HIR::GenericPath& path, const HIR::Trait& trait, const RcString& name, const Monomorphiser& ms) {
            auto it = trait.m_types.find(name);
            if (it != trait.m_types.end()) {
                return ms.monomorph_genericpath(sp, path, /*allow_infer=*/false);
            }
            auto self_ty = g_crate_ptr->m_types.self();
            auto cb = MonomorphStatePtr(g_crate_ptr->m_types, self_ty, &path.m_params, nullptr);
            for (const auto& st : trait.m_all_parent_traits) {
                // NOTE: st.m_trait_ptr isn't populated yet
                const auto& t = g_crate_ptr->get_trait_by_path(sp, st.m_path.m_path);

                auto it = t.m_types.find(name);
                if (it != t.m_types.end()) {
                    // Monomorphse into outer scope, then run the outer monomorph
                    auto p = cb.monomorph_genericpath(sp, st.m_path, /*allow_infer=*/false);
                    return ms.monomorph_genericpath(sp, p, /*allow_infer=*/false);
                }
            }
            return ::HIR::GenericPath();
        }

        static ::HIR::GenericPath find_source_trait_ast(const Span& sp, const ::HIR::GenericPath& path, const AST::Trait& trait, const RcString& name, const Monomorphiser& ms) {
            for (const auto& i : trait.items()) {
                if (i.data.is_Type() && i.name == name) {
                    // Return current path.
                    return ms.monomorph_genericpath(sp, path, /*allow_infer=*/false);
                }
            }

            auto self_ty = g_crate_ptr->m_types.self();
            auto cb = MonomorphStatePtr(g_crate_ptr->m_types, self_ty, &path.m_params, nullptr);
            for (const auto& st : trait.supertraits()) {
                auto b = LowerHIR_TraitPath(sp, *st.ent.path, st.ent.hrbs, true, st.ent.constness);
                ASSERT_BUG(sp, st.ent.path->m_bindings.type.binding.is_Trait(), "Not a trait: " << *st.ent.path);
                auto rv = H::find_source_trait(sp, b.m_path, st.ent.path->m_bindings.type.binding.as_Trait(), name, cb);
                if (rv != HIR::GenericPath()) {
                    return ms.monomorph_genericpath(sp, rv, /*allow_infer=*/false);
                }
            }
            return ::HIR::GenericPath();
        }

        static ::HIR::GenericPath find_source_trait(const Span& sp, const ::HIR::GenericPath& path, const AST::PathBinding_Type& pb, const RcString& name, const Monomorphiser& ms) {
            TRACE_FUNCTION_F(path);
            if (pb.is_Trait()) {
                const auto& pbe = pb.as_Trait();
                if (pbe.hir) {
                    assert(pbe.hir);
                    return find_source_trait_hir(sp, path, *pbe.hir, name, ms);
                } else if (pbe.trait_) {
                    assert(pbe.trait_);
                    return find_source_trait_ast(sp, path, *pbe.trait_, name, ms);
                } else {
                    BUG(sp, "Unbound path");
                }
            } else if (pb.is_TraitAlias()) {
                const auto& pbe = pb.as_TraitAlias();
                if (pbe.hir) {
                    for (const auto& sub_trait : pbe.hir->m_traits) {
                        auto p = ms.monomorph_genericpath(sp, sub_trait.m_path);
                        const auto& t = g_crate_ptr->get_trait_by_path(sp, p.m_path);
                        auto self_ty = g_crate_ptr->m_types.self();
                        auto rv = find_source_trait_hir(sp, p, t, name, MonomorphStatePtr(g_crate_ptr->m_types, self_ty, &p.m_params, nullptr));
                        if (rv != HIR::GenericPath()) {
                            return rv;
                        }
                    }
                    return HIR::GenericPath();
                } else if (pbe.trait_) {
                    auto self_ty = g_crate_ptr->m_types.self();
                    auto cb = MonomorphStatePtr(g_crate_ptr->m_types, self_ty, &path.m_params, nullptr);
                    for (const auto& st : pbe.trait_->traits) {
                        auto b = LowerHIR_TraitPath(sp, *st.ent.path, st.ent.hrbs, true, st.ent.constness);
                        auto rv = H::find_source_trait(sp, b.m_path, st.ent.path->m_bindings.type.binding, name, cb);
                        if (rv != HIR::GenericPath()) {
                            return ms.monomorph_genericpath(sp, rv, /*allow_infer=*/false);
                        }
                    }
                    return HIR::GenericPath();
                } else {
                    BUG(sp, "Unbound path");
                }
            } else {
                BUG(sp, "Not a trait: " << path << " : " << pb.tag_str());
            }
        }

        static std::pair<RcString, HIR::PathParams> get_aty_node(const Span& sp, const ::AST::PathNode& pn) {
            auto args = LowerHIR_PathParams(sp, pn.args(), false);
            if (args.has_params()) {
                TODO(sp, "Handle ATYs with args");
            }
            return std::make_pair(pn.name(), std::move(args));
        }
    };

    for (const auto& e : path.nodes().back().args().m_entries) {
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
                auto name_args = H::get_aty_node(sp, assoc.first);
                auto src_trait = H::find_source_trait(sp, rv.m_path, path.m_bindings.type.binding, name_args.first, MonomorphiserNop(g_crate_ptr->m_types));
                DEBUG("src_trait = " << src_trait << " for " << assoc.first);
                rv.m_type_bounds.insert(::std::make_pair(name_args.first, ::HIR::TraitPath::AtyEqual{std::move(src_trait), std::move(name_args.second), LowerHIR_Type(assoc.second)}));
            }
            TU_ARMA(AssociatedTyBound, assoc) {
                if (!ignore_bounds) {
                    ERROR(sp, E0000, "Associated type trait bounds not allowed here - " << path);
                } else {
                    auto name_args = H::get_aty_node(sp, assoc.first);
                    auto src_trait = H::find_source_trait(sp, rv.m_path, path.m_bindings.type.binding, name_args.first, MonomorphiserNop(g_crate_ptr->m_types));
                    DEBUG("src_trait = " << src_trait << " for " << assoc.first);
                    //if(src_trait == ::HIR::GenericPath())
                    //    ERROR(sp, E0000, "Unable to find source trait for " << b->first << " in " << bound_trait_path.m_path);
                    auto it = rv.m_trait_bounds.insert(std::make_pair(name_args.first, ::HIR::TraitPath::AtyBound{std::move(src_trait), std::move(name_args.second), {}}));
                    for (const auto& trait : assoc.second) {
                        it.first->second.traits.push_back(LowerHIR_TraitPath(sp, trait, {}, /*ignore_bounds*/ false));
                    }
                }
            }
        }
    }

    return rv;
}

::HIR::Path LowerHIR_Path(const Span& sp, const ::AST::Path& path, FromAST_PathClass pc) {
    TU_MATCH_HDRA( (path.m_class), {)
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
            return ::HIR::Path(LowerHIR_GenericPath(sp, path, pc));
        }
        TU_ARMA(UFCS, e) {
            if (e.nodes.size() == 0) {
                if (!(!e.trait || e.trait->is_valid())) {
                    TODO(sp, "Handle UFCS w/ trait and no nodes - " << path);
                }
                auto type = LowerHIR_Type(*e.type);
                ASSERT_BUG(sp, type->is_Path(), "No nodes and non-Path type - " << path);
                return type->as_Path().path.clone();
            }
            if (e.nodes.size() > 1) {
                TODO(sp, "Handle UFCS with multiple nodes - " << path);
            }
            // - No associated type bounds allowed in UFCS paths
            auto params = LowerHIR_PathParams(sp, e.nodes.front().args(), /*allow_assoc*/ false);
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
            if (!e.trait || !e.trait->is_valid()) {
                return ::HIR::Path(::HIR::Path::Data::make_UfcsUnknown({LowerHIR_Type(*e.type), e.nodes[0].name(), mv$(params)}));
            } else {
                return ::HIR::Path(::HIR::Path::Data::make_UfcsKnown({LowerHIR_Type(*e.type), LowerHIR_GenericPath(sp, *e.trait, FromAST_PathClass::Type), e.nodes[0].name(), mv$(params)}));
            }
        }
    }
    throw "BUGCHECK: Reached end of LowerHIR_Path";
}

namespace {
    struct ImplTraitSource {
        const ::HIR::ItemPath* path;
        const ::HIR::GenericParams* params_outer;
        const ::HIR::GenericParams* params_inner = nullptr;

        ImplTraitSource(const ::HIR::ItemPath* path, const ::HIR::GenericParams* params_outer, const ::HIR::GenericParams* params_inner = nullptr)
            : path(path)
            , params_outer(params_outer)
            , params_inner(params_inner)
        {
        }

        ImplTraitSource()
            : path(nullptr)
            , params_outer(nullptr)
        {
        }
    } g_impl_trait_source;

    class TraitObjectLowering {
        const Span& m_span;
        ::HIR::TypeData::Data_TraitObject& m_out;
        ::std::unordered_set<const void*> m_active_aliases;
        ::std::vector<::HIR::LifetimeDef> m_active_hrtbs;

        ::HIR::TraitPath rebase_bound_hrtbs(::HIR::TraitPath trait) const {
            if (m_active_hrtbs.empty() || !trait.m_hrtbs) {
                return trait;
            }

            ::HIR::PathParams shifted;
            shifted.m_lifetimes.reserve(trait.m_hrtbs->m_lifetimes.size());
            for (size_t i = 0; i < trait.m_hrtbs->m_lifetimes.size(); i++) {
                const auto binding = ::HIR::GenericRef(
                    RcString(),
                    ::HIR::GENERIC_Hrtb,
                    static_cast<uint16_t>(m_active_hrtbs.size() + i)).binding;
                shifted.m_lifetimes.push_back(::HIR::LifetimeRef(binding));
            }
            return MonomorphHrlsOnly(g_crate_ptr->m_types, shifted).monomorph_traitpath(m_span, trait, false, true);
        }

        void attach_active_hrtbs(::HIR::TraitPath& trait) const {
            if (m_active_hrtbs.empty()) {
                return;
            }

            ::HIR::GenericParams merged;
            merged.m_lifetimes.reserve(m_active_hrtbs.size()
                + (trait.m_hrtbs ? trait.m_hrtbs->m_lifetimes.size() : 0));
            for (const auto& lifetime : m_active_hrtbs) {
                merged.m_lifetimes.push_back(lifetime);
            }
            if (trait.m_hrtbs) {
                ASSERT_BUG(m_span,
                    trait.m_hrtbs->m_types.empty() && trait.m_hrtbs->m_values.empty() && trait.m_hrtbs->m_bounds.empty(),
                    "Non-lifetime parameters in higher-ranked trait bound");
                for (const auto& lifetime : trait.m_hrtbs->m_lifetimes) {
                    merged.m_lifetimes.push_back(lifetime);
                }
            }
            trait.m_hrtbs = box$(mv$(merged));
        }

        bool has_principal() const {
            return !m_out.m_trait.m_path.m_path.components().empty();
        }

        void add_trait(::HIR::TraitPath trait, bool is_marker) {
            if (is_marker) {
                if (!trait.m_type_bounds.empty() || !trait.m_trait_bounds.empty()) {
                    ERROR(m_span, E0000, "Associated type bounds on auto trait " << trait.m_path);
                }
                m_out.m_markers.push_back(mv$(trait.m_path));
                return;
            }

            attach_active_hrtbs(trait);
            if (has_principal()) {
                ERROR(m_span, E0000, "Multiple data traits in trait object: "
                    << m_out.m_trait.m_path << " and " << trait.m_path);
            }
            m_out.m_trait = mv$(trait);
        }

        void apply_alias_bounds(::HIR::TraitPath& alias_path, bool had_principal) {
            const bool added_principal = !had_principal && has_principal();
            if ((!alias_path.m_type_bounds.empty() || !alias_path.m_trait_bounds.empty()) && !added_principal) {
                ERROR(m_span, E0000, "Associated type bounds on trait alias without a data trait: " << alias_path.m_path);
            }
            if (added_principal) {
                for (auto& bound : alias_path.m_type_bounds) {
                    m_out.m_trait.m_type_bounds.insert(::std::make_pair(bound.first, mv$(bound.second)));
                }
                for (auto& bound : alias_path.m_trait_bounds) {
                    m_out.m_trait.m_trait_bounds.insert(::std::make_pair(bound.first, mv$(bound.second)));
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

        ActiveAlias enter_alias(const void* key, const ::HIR::GenericPath& path) {
            if (!m_active_aliases.insert(key).second) {
                ERROR(m_span, E0000, "Recursive trait alias in trait object: " << path);
            }
            return ActiveAlias{m_active_aliases, key};
        }

        struct ActiveHrtbs {
            ::std::vector<::HIR::LifetimeDef>& lifetimes;
            size_t old_size;

            ~ActiveHrtbs() {
                lifetimes.resize(old_size);
            }
        };

        ActiveHrtbs enter_hrtbs(::HIR::TraitPath& path) {
            const size_t old_size = m_active_hrtbs.size();
            if (path.m_hrtbs) {
                ASSERT_BUG(m_span,
                    path.m_hrtbs->m_types.empty() && path.m_hrtbs->m_values.empty() && path.m_hrtbs->m_bounds.empty(),
                    "Non-lifetime parameters in higher-ranked trait alias");
                m_active_hrtbs.reserve(old_size + path.m_hrtbs->m_lifetimes.size());
                for (const auto& lifetime : path.m_hrtbs->m_lifetimes) {
                    m_active_hrtbs.push_back(lifetime);
                }
                path.m_hrtbs.reset();
            }
            return ActiveHrtbs{m_active_hrtbs, old_size};
        }

        void add_ast_path(::HIR::TraitPath path, const ::AST::PathBinding_Type& binding) {
            if (const auto* trait = binding.opt_Trait()) {
                ASSERT_BUG(m_span, trait->trait_ || trait->hir, "Null trait binding for " << path.m_path);
                add_trait(mv$(path), trait->trait_ ? trait->trait_->is_marker() : trait->hir->m_is_marker);
            } else if (const auto* alias = binding.opt_TraitAlias()) {
                expand_ast_alias(mv$(path), *alias);
            } else {
                BUG(m_span, "Not a trait or trait alias: " << path.m_path << " (" << binding.tag_str() << ")");
            }
        }

        void add_hir_path(::HIR::TraitPath path) {
            const auto& item = g_crate_ptr->get_typeitem_by_path(m_span, path.m_path.m_path);
            if (const auto* trait = item.opt_Trait()) {
                add_trait(mv$(path), trait->m_is_marker);
            } else if (const auto* alias = item.opt_TraitAlias()) {
                expand_hir_alias(mv$(path), *alias);
            } else {
                BUG(m_span, "Trait alias expanded to non-trait path " << path.m_path << " (" << item.tag_str() << ")");
            }
        }

        void expand_ast_alias(::HIR::TraitPath alias_path, const ::AST::PathBinding_Type::Data_TraitAlias& binding) {
            const void* key = binding.trait_ ? static_cast<const void*>(binding.trait_) : static_cast<const void*>(binding.hir);
            ASSERT_BUG(m_span, key, "Null trait alias binding for " << alias_path.m_path);
            auto active = enter_alias(key, alias_path.m_path);
            auto active_hrtbs = enter_hrtbs(alias_path);
            const bool had_principal = has_principal();

            if (binding.trait_) {
                bool trait_requires_sized = false;
                auto params_def = LowerHIR_GenericParams(binding.trait_->params, &trait_requires_sized);
                auto params = ConvertHIR_CompleteAliasParams(g_crate_ptr->m_types, m_span, params_def, alias_path.m_path, false);
                auto monomorph = MonomorphStatePtr(g_crate_ptr->m_types, nullptr, &params, nullptr);
                for (const auto& bound : binding.trait_->traits) {
                    auto trait = rebase_bound_hrtbs(LowerHIR_TraitPath(bound.sp, *bound.ent.path, bound.ent.hrbs, false, bound.ent.constness));
                    add_ast_path(
                        monomorph.monomorph_traitpath(m_span, trait, false),
                        bound.ent.path->m_bindings.type.binding);
                }
            } else {
                ASSERT_BUG(m_span, binding.hir, "Null trait alias binding for " << alias_path.m_path);
                expand_hir_alias_contents(alias_path, *binding.hir);
            }

            apply_alias_bounds(alias_path, had_principal);
        }

        void expand_hir_alias_contents(const ::HIR::TraitPath& alias_path, const ::HIR::TraitAlias& alias) {
            auto params = ConvertHIR_CompleteAliasParams(g_crate_ptr->m_types, m_span, alias.m_params, alias_path.m_path, false);
            auto monomorph = MonomorphStatePtr(g_crate_ptr->m_types, nullptr, &params, nullptr);
            for (const auto& bound : alias.m_traits) {
                auto trait = rebase_bound_hrtbs(bound.clone());
                add_hir_path(monomorph.monomorph_traitpath(m_span, trait, false));
            }
        }

        void expand_hir_alias(::HIR::TraitPath alias_path, const ::HIR::TraitAlias& alias) {
            auto active = enter_alias(&alias, alias_path.m_path);
            auto active_hrtbs = enter_hrtbs(alias_path);
            const bool had_principal = has_principal();
            expand_hir_alias_contents(alias_path, alias);
            apply_alias_bounds(alias_path, had_principal);
        }

    public:
        TraitObjectLowering(const Span& span, ::HIR::TypeData::Data_TraitObject& out)
            : m_span(span)
            , m_out(out)
        {
        }

        void add(const ::Type_TraitPath& bound) {
            auto path = LowerHIR_TraitPath(m_span, *bound.path, bound.hrbs, false, bound.constness);
            add_ast_path(mv$(path), bound.path->m_bindings.type.binding);
        }
    };
}

::HIR::TypeRef LowerHIR_Type(const ::TypeRef& ty) {
    TU_MATCH_HDRA( (ty.m_data), {)
    TU_ARMA(None, e) {
            BUG(ty.span(), "TypeData::None");
        }
        TU_ARMA(Bang, e) {
            return g_crate_ptr->m_types.diverge();
        }
        TU_ARMA(Any, e) {
            return g_crate_ptr->m_types.infer();
        }
        TU_ARMA(Unit, e) {
            return g_crate_ptr->m_types.unit();
        }
        TU_ARMA(Macro, e) {
            BUG(ty.span(), "TypeData::Macro");
        }
        TU_ARMA(Primitive, e) {
            switch (e.core_type) {
                case CORETYPE_BOOL:
                    return g_crate_ptr->m_types.primitive(::HIR::CoreType::Bool);
                case CORETYPE_CHAR:
                    return g_crate_ptr->m_types.primitive(::HIR::CoreType::Char);
                case CORETYPE_STR:
                    return g_crate_ptr->m_types.primitive(::HIR::CoreType::Str);
                case CORETYPE_F16:
                    return g_crate_ptr->m_types.primitive(::HIR::CoreType::F16);
                case CORETYPE_F32:
                    return g_crate_ptr->m_types.primitive(::HIR::CoreType::F32);
                case CORETYPE_F64:
                    return g_crate_ptr->m_types.primitive(::HIR::CoreType::F64);
                case CORETYPE_F128:
                    return g_crate_ptr->m_types.primitive(::HIR::CoreType::F128);

                case CORETYPE_I8:
                    return g_crate_ptr->m_types.primitive(::HIR::CoreType::I8);
                case CORETYPE_U8:
                    return g_crate_ptr->m_types.primitive(::HIR::CoreType::U8);
                case CORETYPE_I16:
                    return g_crate_ptr->m_types.primitive(::HIR::CoreType::I16);
                case CORETYPE_U16:
                    return g_crate_ptr->m_types.primitive(::HIR::CoreType::U16);
                case CORETYPE_I32:
                    return g_crate_ptr->m_types.primitive(::HIR::CoreType::I32);
                case CORETYPE_U32:
                    return g_crate_ptr->m_types.primitive(::HIR::CoreType::U32);
                case CORETYPE_I64:
                    return g_crate_ptr->m_types.primitive(::HIR::CoreType::I64);
                case CORETYPE_U64:
                    return g_crate_ptr->m_types.primitive(::HIR::CoreType::U64);

                case CORETYPE_I128:
                    return g_crate_ptr->m_types.primitive(::HIR::CoreType::I128);
                case CORETYPE_U128:
                    return g_crate_ptr->m_types.primitive(::HIR::CoreType::U128);

                case CORETYPE_INT:
                    return g_crate_ptr->m_types.primitive(::HIR::CoreType::Isize);
                case CORETYPE_UINT:
                    return g_crate_ptr->m_types.primitive(::HIR::CoreType::Usize);
                case CORETYPE_ANY:
                    TODO(ty.span(), "TypeData::Primitive - CORETYPE_ANY");
                case CORETYPE_INVAL:
                    BUG(ty.span(), "TypeData::Primitive - CORETYPE_INVAL");
            }
        }
        TU_ARMA(Tuple, e) {
            ::HIR::TypeData::Data_Tuple v;
            for (const auto& st : e.inner_types) {
                v.push_back(LowerHIR_Type(st));
            }
            return g_crate_ptr->m_types.tuple(mv$(v));
        }
        TU_ARMA(Borrow, e) {
            auto cl = (e.is_mut ? ::HIR::BorrowType::Unique : ::HIR::BorrowType::Shared);
            return g_crate_ptr->m_types.borrow(cl, LowerHIR_Type(*e.inner), LowerHIR_LifetimeRef(e.lifetime));
        }
        TU_ARMA(Pointer, e) {
            auto cl = (e.is_mut ? ::HIR::BorrowType::Unique : ::HIR::BorrowType::Shared);
            return g_crate_ptr->m_types.pointer(cl, LowerHIR_Type(*e.inner));
        }
        TU_ARMA(Array, e) {
            auto inner = LowerHIR_Type(*e.inner);
            if (e.size) {
                // If the size expression is an unannotated or usize integer literal, don't bother converting the expression
                if (const auto* ptr = cast<const ::AST::ExprNode_Integer>(&*e.size)) {
                    if (ptr->m_datatype == CORETYPE_UINT || ptr->m_datatype == CORETYPE_ANY) {
                        // TODO: Chage the HIR format to support very large arrays
                        if (ptr->m_value >= U128(UINT64_MAX)) {
                            ERROR(ty.span(), E0000, "Array size out of bounds - 0x" << ::std::hex << ptr->m_value << " > 0x" << UINT64_MAX << " in " << ::std::dec << ty);
                        }
                        return g_crate_ptr->m_types.array(inner, ptr->m_value.truncate_u64());
                    }
                }
                if (const auto* ptr = cast<const ::AST::ExprNode_NamedValue>(&*e.size)) {
                    if (ptr->m_path.is_trivial()) {
                        auto gr = HIR::GenericRef(ptr->m_path.as_trivial(), ptr->m_path.m_bindings.value.binding.as_Generic().index);
                        return g_crate_ptr->m_types.array(inner, HIR::ConstGeneric(mv$(gr)));
                    }
                }

                return g_crate_ptr->m_types.array(inner, HIR::ConstGeneric::make_Unevaluated(std::make_unique<HIR::ConstGeneric_Unevaluated>(LowerHIR_Expr(e.size))));
            } else {
                return g_crate_ptr->m_types.array(inner, HIR::ConstGeneric::make_Infer({}));
            }
        }
        TU_ARMA(Slice, e) {
            auto inner = LowerHIR_Type(*e.inner);
            return g_crate_ptr->m_types.slice(inner);
        }
        TU_ARMA(Path, e) {
            if (const auto* l = e->m_class.opt_Local()) {
                unsigned int slot;
                // NOTE: TypeParameter is unused
                if (const auto* p = e->m_bindings.type.binding.opt_TypeParameter()) {
                    slot = p->slot;
                } else {
                    BUG(ty.span(), "Unbound local encountered in " << *e);
                }
                return g_crate_ptr->m_types.generic(l->name, slot);
            } else if (e->m_bindings.type.path.crate == CRATE_BUILTINS) {
                return LowerHIR_Type(TypeRef(ty.span(), coretype_fromstring(e->m_bindings.type.path.nodes.back().c_str())));
            } else {
                return g_crate_ptr->m_types.path(LowerHIR_Path(ty.span(), *e, FromAST_PathClass::Type), {});
            }
        }
        TU_ARMA(TraitObject, e) {
            ::HIR::TypeData::Data_TraitObject v;
            if (e.lifetimes.empty()) {
                // Lifetime elision should have handled this?
            } else if (e.lifetimes.size() == 1) {
                v.m_lifetime = LowerHIR_LifetimeRef(e.lifetimes[0]);
            } else {
                BUG(ty.span(), "Handle multiple lifetimes on a trait object - " << ty);
            }
            TraitObjectLowering lowering(ty.span(), v);
            for (const auto& t : e.traits) {
                DEBUG("t = " << *t.path);
                lowering.add(t);
            }
            // Sort markers so downstream can compare properly
            ::std::sort(v.m_markers.begin(), v.m_markers.end());
            v.m_markers.erase(::std::unique(v.m_markers.begin(), v.m_markers.end()), v.m_markers.end());
            return g_crate_ptr->m_types.intern(::HIR::TypeData::make_TraitObject(mv$(v)));
        }
        TU_ARMA(ErasedType, e) {
            ASSERT_BUG(ty.span(), e->traits.size() > 0, "ErasedType with no traits");

            // TODO: There can be associated type bounds, those need to be propagated

            ::std::vector<::HIR::TraitPath> traits;
            for (const auto& t : e->traits) {
                DEBUG("t = " << *t.path);
                // TODO: Handle ATY bounds
                traits.push_back(LowerHIR_TraitPath(ty.span(), *t.path, t.hrbs, /*allow_aty_trait_bounds=*/true, t.constness));
            }
            bool is_sized = true;
            for (const auto& t : e->maybe_traits) {
                auto tp = LowerHIR_TraitPath(ty.span(), *t.path, t.hrbs, /*allow_aty_trait_bounds=*/true);
                if (tp.m_path.m_path == path_Sized) {
                    is_sized = false;
                } else {
                    TODO(ty.span(), "Optional trait (not Sized) - " << ty);
                }
            }
            std::vector<::HIR::LifetimeRef> lfts;
            for (const auto& lft : e->lifetimes) {
                lfts.push_back(LowerHIR_LifetimeRef(lft));
            }
            ::HIR::TypeData_ErasedType_Inner inner;
            if (g_impl_trait_source.path) {
                if (g_impl_trait_source.params_inner && g_impl_trait_source.params_inner->is_generic()) {
                    TODO(ty.span(), "Handle multi-layered generic erased type (used in a GAT)");
                }
                inner = ::HIR::TypeData_ErasedType_Inner(::HIR::TypeData_ErasedType_Inner::Data_Alias{g_impl_trait_source.params_outer->make_nop_params(g_crate_ptr->m_types, 0), std::make_shared<HIR::TypeData_ErasedType_AliasInner>(*g_impl_trait_source.path, *g_impl_trait_source.params_outer)});
            } else {
                inner = ::HIR::TypeData_ErasedType_Inner::Data_Fcn{::HIR::Path(::HIR::SimplePath()), 0}; // Populated in bind, could be populated now?
            }
            return g_crate_ptr->m_types.intern(::HIR::TypeData::make_ErasedType({is_sized, mv$(traits), mv$(lfts), mv$(inner), e->use ? LowerHIR_PathParams(ty.span(), *e->use, false) : HIR::PathParams(), e->use ? ::HIR::TypeData_ErasedType::Use::Present : (e->is_edition_2024_or_later ? ::HIR::TypeData_ErasedType::Use::Omitted2024 : ::HIR::TypeData_ErasedType::Use::OmittedOld)}));
        }
        TU_ARMA(Function, e) {
            HIR::GenericParams params;
            for (const auto& lft_def : e.info.hrbs.m_lifetimes) {
                params.m_lifetimes.push_back(HIR::LifetimeDef{lft_def.name().name});
            }
            ::std::vector<::HIR::TypeRef> args;
            for (const auto& arg : e.info.m_arg_types) {
                args.push_back(LowerHIR_Type(arg));
            }
            ::HIR::TypeData_FunctionPointer f{mv$(params), e.info.is_unsafe, e.info.is_variadic, RcString::new_interned(e.info.m_abi), LowerHIR_Type(*e.info.m_rettype), mv$(args)};
            if (f.m_abi == "") {
                f.m_abi = RcString::new_interned(ABI_RUST);
            }
            return g_crate_ptr->m_types.function(mv$(f));
        }
        TU_ARMA(Generic, e) {
            assert(e.index < 0x10000);
            return g_crate_ptr->m_types.generic(e.name, e.index);
        }
    }
    throw "BUGCHECK: Reached end of LowerHIR_Type";
}

::HIR::TypeAlias LowerHIR_TypeAlias(const HIR::ItemPath& p, const ::AST::TypeAlias& ta) {
    assert(!g_impl_trait_source.path);
    auto params = LowerHIR_GenericParams(ta.params(), nullptr);
    g_impl_trait_source = ImplTraitSource(&p, &params);
    auto ty = LowerHIR_Type(ta.type());
    //if( auto* e = ty.data_mut().opt_ErasedType() ) {
    //    DEBUG("Flag type alias - " << &ty.data());
    //    e->m_inner = std::make_shared<HIR::TypeData_ErasedType_AliasInner>(p);
    //}
    g_impl_trait_source = ImplTraitSource();
    return ::HIR::TypeAlias{std::move(params), ::std::move(ty)};
}

namespace {
    template <typename T>
    ::HIR::VisEnt<T> new_visent(HIR::Publicity pub, T v) {
        return ::HIR::VisEnt<T>{pub, mv$(v)};
    }

    ::HIR::SimplePath get_parent_module(const ::HIR::ItemPath& p) {
        const ::HIR::ItemPath* parent_ip = p.parent;
        assert(parent_ip);
        while (parent_ip->name && parent_ip->name[0] == '#') {
            parent_ip = parent_ip->parent;
            assert(parent_ip);
        }
        return parent_ip->get_simple_path();
    }
}

::HIR::t_struct_fields LowerHIR_StructFields(::HIR::ItemPath path, const ::HIR::GenericParams& params, const ::std::vector<AST::StructItem>& in_fields, ::HIR::Module& out_mod) {
    ::HIR::Struct::Data::Data_Named fields;
    for (const auto& field : in_fields) {
        auto type = LowerHIR_Type(field.m_type);
        ::std::unique_ptr<HIR::GenericPath> field_default;
        if (field.m_default) {
            // NOTE: I'd love to have this be a `Constant`, but that would require duplicating the type and the params
            // meh. Lazy option is to just duplicate
            auto name = RcString::new_interned(FMT(path.get_name() << "#default_" << field.m_name));
            out_mod.m_value_items.insert(std::make_pair(name, ::std::make_unique<HIR::VisEnt<HIR::ValueItem>>(HIR::VisEnt<HIR::ValueItem>{HIR::Publicity::new_global(), HIR::ValueItem(HIR::Constant(params.clone(), type, LowerHIR_Expr(field.m_default)))})));
            field_default = std::make_unique<HIR::GenericPath>((*path.parent + name).get_simple_path(), params.make_nop_params(g_crate_ptr->m_types, 0));
        }
        fields.push_back(HIR::StructField{field.m_name, LowerHIR_Vis(get_parent_module(path), field.m_vis), std::move(type), std::move(field_default)});
    }
    return fields;
}

::HIR::Struct LowerHIR_Struct(const Span& sp, ::HIR::ItemPath path, const ::AST::Struct& ent, const ::AST::AttributeList& attrs, ::HIR::Module& out_mod) {
    TRACE_FUNCTION_F(path);
    ::HIR::Struct::Data data;

    auto mod_path = get_parent_module(path);
    auto get_vis = [&](const AST::Visibility& vis) {
        return LowerHIR_Vis(mod_path, vis);
    };

    auto rv = ::HIR::Struct{LowerHIR_GenericParams(ent.params(), nullptr), ::HIR::Struct::Repr::Rust, {}};

    TU_MATCH_HDRA( (ent.m_data), {)
    TU_ARMA(Unit, e) {
            rv.m_data = ::HIR::Struct::Data::make_Unit({});
        }
        TU_ARMA(Tuple, e) {
            ::HIR::Struct::Data::Data_Tuple fields;

            for (const auto& field : e.ents) {
                fields.push_back({get_vis(field.m_vis), LowerHIR_Type(field.m_type)});
            }

            rv.m_data = ::HIR::Struct::Data::make_Tuple(mv$(fields));
        }
        TU_ARMA(Struct, e) {
            auto fields = LowerHIR_StructFields(path, rv.m_params, e.ents, out_mod);
            rv.m_data = ::HIR::Struct::Data::make_Named(mv$(fields));
        }
    }

    // Determine the repr
    {
        switch (ent.m_markings.repr) {
            case AST::Struct::Markings::Repr::Rust:
                rv.m_repr = ::HIR::Struct::Repr::Rust;
                break;
            case AST::Struct::Markings::Repr::C:
                rv.m_repr = ::HIR::Struct::Repr::C;
                break;
            case AST::Struct::Markings::Repr::Simd:
                rv.m_repr = ::HIR::Struct::Repr::Simd;
                //ASSERT_BUG(sp, ent.m_markings.max_field_align == 0, "packed() on simd?");
                break;
            case AST::Struct::Markings::Repr::Transparent:
                rv.m_repr = ::HIR::Struct::Repr::Transparent;
                ASSERT_BUG(sp, ent.m_markings.max_field_align == 0, "packed() on transparent?");
                break;
        }
        rv.m_forced_alignment = ent.m_markings.align_value;
        rv.m_max_field_alignment = ent.m_markings.max_field_align;
    }

    // #[rustc_nonnull_optimization_guaranteed]
    // TODO: OR, it's equal to the `non_zero` lang item
    if(attrs.get("rustc_nonnull_optimization_guaranteed"))
    {
        //ent.m_markings.scalar_valid_start_set = true;
        // In 1.90 this no longer marks wrappers as nonzero; scalar limits carry
        // the layout information instead.
    }
    rv.m_struct_markings.is_fundamental = attrs.has("fundamental");
    if(ent.m_markings.scalar_valid_start_set)
    {
        if (ent.m_markings.scalar_valid_start == U128(1)) {
            rv.m_struct_markings.is_nonzero = true;
        } else {
            //TODO(sp, "Handle #[rustc_layout_scalar_valid_range_start(" << ent.m_markings.scalar_valid_start << ")]");
        }
    }
    // TODO: Store the scalar valid range information for downstream
    if( ent.m_markings.scalar_valid_start_set || ent.m_markings.scalar_valid_end_set )
    {
        const HIR::TypeData* ty = nullptr;
        const HIR::TypeData* ty2 = nullptr;
        if (const auto* d = rv.m_data.opt_Named()) {
            switch (d->size()) {
                case 2:
                    ty2 = (*d)[1].ty;
                case 1:
                    ty = (*d)[0].ty;
                    break;
            }
        } else if (const auto* d = rv.m_data.opt_Tuple()) {
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

        uint64_t TGT_PTR_MAX = Target_GetPointerBits() == 64 ? UINT64_MAX : UINT32_MAX;
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
            if (ent.m_markings.scalar_valid_start_set) {
                if (ent.m_markings.scalar_valid_start < min) {
                }
                //rv.m_struct_markings.bounded_min = true;
                //rv.m_struct_markings.bounded_min_value = ent.m_markings.scalar_valid_start;
            }
            if (ent.m_markings.scalar_valid_end_set) {
                if (ent.m_markings.scalar_valid_end > max) {
                }
                rv.m_struct_markings.bounded_max = true;
                rv.m_struct_markings.bounded_max_value = ent.m_markings.scalar_valid_end;
            }
        }
    }

    return rv;
}

::HIR::Enum LowerHIR_Enum(::HIR::ItemPath path, const ::AST::Enum& ent, const ::AST::AttributeList& attrs, ::std::function<void(RcString, ::HIR::Struct)> push_struct, HIR::Module& out_mod) {
    // 1. Figure out what sort of enum this is (value or data)
    bool has_data = false;
    for (const auto& var : ent.variants()) {
        if (var.m_data.is_Tuple() || var.m_data.is_Struct()) {
            has_data = true;
        } else {
            // Unit-like
            assert(var.m_data.is_Unit());
        }
    }
    if (ent.m_markings.align_value != 0) {
        has_data = true;
    }

    bool is_repr_c = ent.m_markings.is_repr_c;
    auto repr = ::HIR::Enum::Repr::Auto;
    switch (ent.m_markings.repr) {
        case ::AST::Enum::Markings::Repr::Rust:
            repr = ::HIR::Enum::Repr::Auto;
            break;
        case ::AST::Enum::Markings::Repr::U8:
            repr = ::HIR::Enum::Repr::U8;
            break;
        case ::AST::Enum::Markings::Repr::U16:
            repr = ::HIR::Enum::Repr::U16;
            break;
        case ::AST::Enum::Markings::Repr::U32:
            repr = ::HIR::Enum::Repr::U32;
            break;
        case ::AST::Enum::Markings::Repr::U64:
            repr = ::HIR::Enum::Repr::U64;
            break;
        case ::AST::Enum::Markings::Repr::U128:
            repr = ::HIR::Enum::Repr::U128;
            break;
        case ::AST::Enum::Markings::Repr::Usize:
            repr = ::HIR::Enum::Repr::Usize;
            break;
        case ::AST::Enum::Markings::Repr::I8:
            repr = ::HIR::Enum::Repr::I8;
            break;
        case ::AST::Enum::Markings::Repr::I16:
            repr = ::HIR::Enum::Repr::I16;
            break;
        case ::AST::Enum::Markings::Repr::I32:
            repr = ::HIR::Enum::Repr::I32;
            break;
        case ::AST::Enum::Markings::Repr::I64:
            repr = ::HIR::Enum::Repr::I64;
            break;
        case ::AST::Enum::Markings::Repr::I128:
            repr = ::HIR::Enum::Repr::I128;
            break;
        case ::AST::Enum::Markings::Repr::Isize:
            repr = ::HIR::Enum::Repr::Isize;
            break;
    }

    auto params = LowerHIR_GenericParams(ent.params(), nullptr);

    ::HIR::Enum::Class data;
    if (ent.variants().size() > 0 && !has_data) {
        ::std::vector<::HIR::Enum::ValueVariant> variants;
        for (const auto& var : ent.variants()) {
            // TODO: Quick consteval on the expression?
            variants.push_back({var.m_name, LowerHIR_Expr(var.m_discriminant_value), U128(0)});
        }

        data = ::HIR::Enum::Class::make_Value({mv$(variants)});
    }
    // NOTE: empty enums are encoded as empty Data enums
    else {
        ::std::vector<::HIR::Enum::DataVariant> variants;
        const auto variant_repr = is_repr_c || repr != ::HIR::Enum::Repr::Auto ? ::HIR::Struct::Repr::C : ::HIR::Struct::Repr::Rust;
        for (const auto& var : ent.variants()) {
            if (var.m_data.is_Unit() && ent.m_markings.align_value == 0) {
                // TODO: Should this make its own unit-like struct?
                variants.push_back({var.m_name, false, g_crate_ptr->m_types.unit()});
            } else {
                ::HIR::Struct::Data data;
                if (var.m_data.is_Unit()) {
                    data = ::HIR::Struct::Data::make_Unit({});
                } else if (const auto* ve = var.m_data.opt_Tuple()) {
                    ::HIR::Struct::Data::Data_Tuple fields;
                    for (const auto& field : ve->m_items) {
                        fields.push_back(new_visent(::HIR::Publicity::new_global(), LowerHIR_Type(field.m_type)));
                    }
                    data = ::HIR::Struct::Data::make_Tuple(mv$(fields));
                } else if (const auto* ve = var.m_data.opt_Struct()) {
                    auto fields = LowerHIR_StructFields(path, params, ve->m_fields, out_mod);
                    data = ::HIR::Struct::Data::make_Named(mv$(fields));
                } else {
                    throw "";
                }

                auto ty_name = RcString::new_interned(FMT(path.name << "#" << var.m_name));
                auto variant_struct = ::HIR::Struct{LowerHIR_GenericParams(ent.params(), nullptr), variant_repr, mv$(data)};
                variant_struct.m_forced_alignment = ent.m_markings.align_value;
                push_struct(ty_name, mv$(variant_struct));
                auto ty_ipath = path;
                ty_ipath.name = ty_name.c_str();
                auto ty_path = ty_ipath.get_full_path();
                // Add type params
                ty_path.m_data.as_Generic().m_params = params.make_nop_params(g_crate_ptr->m_types, 0);
                variants.push_back({var.m_name, var.m_data.is_Struct(), g_crate_ptr->m_types.path(mv$(ty_path), {})});
            }

            if (var.m_discriminant_value) {
                if (repr == ::HIR::Enum::Repr::Auto) {
                    ERROR(var.m_discriminant_value.node().span(), E0000, "Discrimiant value set on enum with no `repr` set");
                }
                variants.back().discriminant_expr = LowerHIR_Expr(var.m_discriminant_value);
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

    return ::HIR::Enum{mv$(params), is_repr_c, repr, mv$(data)};
}

::HIR::Union LowerHIR_Union(::HIR::ItemPath path, const ::AST::Union& f, const ::AST::AttributeList& attrs) {
    auto mod_path = get_parent_module(path);
    auto get_vis = [&](const AST::Visibility& vis) {
        return LowerHIR_Vis(mod_path, vis);
    };

    auto repr = ::HIR::Union::Repr::Rust;
    switch (f.m_markings.repr) {
        case ::AST::Union::Markings::Repr::Rust:
            repr = ::HIR::Union::Repr::Rust;
            break;
        case ::AST::Union::Markings::Repr::C:
            repr = ::HIR::Union::Repr::C;
            break;
        case ::AST::Union::Markings::Repr::Transparent:
            repr = ::HIR::Union::Repr::Transparent;
            break;
    }

    ::HIR::Struct::Data::Data_Named variants;
    for (const auto& field : f.m_variants) {
        variants.push_back(HIR::StructField{field.m_name, get_vis(field.m_vis), LowerHIR_Type(field.m_type), {}});
    }

    return ::HIR::Union{LowerHIR_GenericParams(f.m_params, nullptr), repr, mv$(variants)};
}

::HIR::Trait LowerHIR_Trait(
    ::HIR::SimplePath trait_path,
    const ::AST::Trait& f,
    const ::AST::AttributeList& attrs
) {
    TRACE_FUNCTION_F(trait_path);
    trait_path.update_crate_name(g_crate_name);

    bool trait_reqires_sized = false;
    auto params = LowerHIR_GenericParams(f.params(), &trait_reqires_sized);

    ::HIR::LifetimeRef lifetime;
    if (!f.lifetimes().empty()) {
        ASSERT_BUG(f.lifetimes()[0].sp, f.lifetimes().size() == 1, "");
        lifetime = LowerHIR_LifetimeRef(f.lifetimes()[0].ent);
        DEBUG("Lifetime " << lifetime << " (" << f.lifetimes()[0].ent << " " << f.lifetimes()[0].ent.binding() << ")");
    }
    ::std::vector<::HIR::TraitPath> supertraits;
    for (const auto& st : f.supertraits()) {
        supertraits.push_back(LowerHIR_TraitPath(st.sp, *st.ent.path, st.ent.hrbs, true, st.ent.constness));
        DEBUG("Supertrait " << supertraits.back());
    }
    ::HIR::Trait rv{mv$(params), mv$(lifetime), mv$(supertraits)};
    rv.m_is_const = attrs.has("const_trait");

    // HACK: Add a bound of Self: ThisTrait for parts of typeck (TODO: Remove this, it's evil)
    {
        auto this_trait = ::HIR::GenericPath(trait_path);
        this_trait.m_params = rv.m_params.make_nop_params(g_crate_ptr->m_types, 0);
        rv.m_params.m_bounds.push_back(::HIR::GenericBound::make_TraitBound({{}, g_crate_ptr->m_types.self(), {{}, mv$(this_trait)}}));
    }

    for (const auto& item : f.items()) {
        auto trait_ip = ::HIR::ItemPath(trait_path);
        auto item_path = ::HIR::ItemPath(trait_ip, item.name.c_str());

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
                bool is_sized = true;
                ::std::vector<::HIR::TraitPath> trait_bounds;
                ::HIR::LifetimeRef lifetime_bound;
                auto gps = LowerHIR_GenericParams(i.params(), &is_sized);

                auto self_bounds = LowerHIR_GenericParams(i.m_self_bounds, &is_sized);
                for (auto& b : self_bounds.m_bounds) {
                TU_MATCH_HDRA( (b), {)
                TU_ARMA(TypeLifetime, be) {
                            ASSERT_BUG(item.span, be.type->as_Generic().binding == GENERIC_Self, be.type);
                            lifetime_bound = mv$(be.valid_for);
                        }
                        TU_ARMA(TraitBound, be) {
                            ASSERT_BUG(item.span, be.type->as_Generic().binding == GENERIC_Self, be.type);
                            trait_bounds.push_back(mv$(be.trait));
                        }
                        TU_ARMA(Lifetime, be) {
                            BUG(item.span, "Unexpected lifetime-lifetime bound on associated type");
                        }
                        TU_ARMA(TypeEquality, be) {
                            BUG(item.span, "Unexpected type equality bound on associated type");
                        }
                }
                }
                rv.m_types.insert(::std::make_pair(item.name, ::HIR::AssociatedType{mv$(gps), is_sized, mv$(lifetime_bound), mv$(trait_bounds), LowerHIR_Type(i.type())}));
            }
            TU_ARMA(Function, i) {
                auto fcn = LowerHIR_Function(item_path, item.attrs, i, g_crate_ptr->m_types.self());
                if (rv.m_is_const) {
                    fcn.m_const = true;
                }
                fcn.m_save_code = true;
                rv.m_values.insert(::std::make_pair(item.name, ::HIR::TraitValueItem::make_Function(mv$(fcn))));
            }
            TU_ARMA(Static, i) {
                if (i.s_class() == ::AST::Static::CONST) {
                    rv.m_values.insert(::std::make_pair(item.name, ::HIR::TraitValueItem::make_Constant(::HIR::Constant(::HIR::GenericParams{}, LowerHIR_Type(i.type()), LowerHIR_Expr(i.value())))));
                } else {
                    ::HIR::Linkage linkage;
                    rv.m_values.insert(::std::make_pair(item.name, ::HIR::TraitValueItem::make_Static(::HIR::Static(mv$(linkage), (i.s_class() == ::AST::Static::MUT), LowerHIR_Type(i.type()), LowerHIR_Expr(i.value())))));
                }
            }
        }
    }

    rv.m_is_marker = f.is_marker();
    rv.m_is_coinductive = rv.m_is_marker || attrs.has("rustc_coinductive");
    rv.m_is_fundamental = attrs.has("fundamental");

    return rv;
}

::HIR::TraitAlias LowerHIR_TraitAlias(const Span& sp, ::HIR::ItemPath p, const ::AST::TraitAlias& f) {
    bool trait_reqires_sized = false;

    HIR::TraitAlias ta;
    ta.m_params = LowerHIR_GenericParams(f.params, &trait_reqires_sized);
    for (const auto& t : f.traits) {
        ta.m_traits.push_back(LowerHIR_TraitPath(t.sp, *t.ent.path, t.ent.hrbs, false, t.ent.constness));
    }

    return ta;
}

::HIR::Function LowerHIR_Function(::HIR::ItemPath p, const ::AST::AttributeList& attrs, const ::AST::Function& f, const ::HIR::TypeData* real_self_type) {
    static Span sp;

    TRACE_FUNCTION_F(p);

    ::std::vector<::std::pair<::HIR::Pattern, ::HIR::TypeRef>> args;
    for (const auto& arg : f.args()) {
        args.push_back(::std::make_pair(LowerHIR_Pattern(arg.pat), LowerHIR_Type(arg.ty)));
    }

    auto receiver = ::HIR::Function::Receiver::Free;

    if (args.size() > 0 && args.front().first.m_bindings.size() > 0 && args.front().first.m_bindings[0].m_name == "self") {
        const auto& sp = f.args()[0].pat.span();
        auto& arg_self_ty = args.front().second;

        struct Ivcr {
            const Span& sp;
            const ::HIR::TypeData* real_self_type;

            Ivcr(const Span& sp, const ::HIR::TypeData* real_self_type)
                : sp(sp)
                , real_self_type(real_self_type)
            {
            }

            bool is_valid_custom_receiver(::HIR::TypeRef& ty) const {
                // - The path must include Self as a (the only?) type param.
                if (ty == g_crate_ptr->m_types.self()) {
                    return true;
                } else if (ty == real_self_type) {
                    ty = g_crate_ptr->m_types.self();
                    return true;
                } else if (ty->is_Path()) {
                    auto data = ty->clone_data();
                    auto& e = data.as_Path();
                    if (auto* pe = e.path.m_data.opt_Generic()) {
                        if (pe->m_params.m_types.size() == 0) {
                            ERROR(sp, E0000, "Receiver type should have one type param - " << ty);
                        }
                        //if( pe->m_params.m_types.size() != 1 ) {
                        //   TODO(sp, "Receiver types with more than one param - " << arg_self_ty);
                        //}

                        // TODO: Allow if the type parm is a valid receiver it type too
                        // - In general, it's valid if there's a deref chain from this type to `self` (maybe could check that in a later pass, instead of erroring here)
                        if (is_valid_custom_receiver(pe->m_params.m_types[0])) {
                            ty = g_crate_ptr->m_types.intern(mv$(data));
                            return true;
                        }
                    }
                    return false;
                } else if (ty->is_Borrow()) {
                    const auto& e = ty->as_Borrow();
                    auto inner = e.inner;
                    if (!is_valid_custom_receiver(inner)) {
                        return false;
                    }
                    ty = g_crate_ptr->m_types.borrow(e.type, inner, e.lifetime);
                    return true;
                } else if (ty->is_Pointer()) {
                    const auto& e = ty->as_Pointer();
                    auto inner = e.inner;
                    if (!is_valid_custom_receiver(inner)) {
                        return false;
                    }
                    ty = g_crate_ptr->m_types.pointer(e.type, inner);
                    return true;
                } else {
                    return false;
                }
            }
        } ivcr(sp, real_self_type);

        if (arg_self_ty == g_crate_ptr->m_types.self() || arg_self_ty == real_self_type) {
            receiver = ::HIR::Function::Receiver::Value;
        } else if (const auto* e = arg_self_ty->opt_Borrow()) {
            if (e->inner == g_crate_ptr->m_types.self() || e->inner == real_self_type) {
                if (e->inner == real_self_type) {
                    arg_self_ty = g_crate_ptr->m_types.borrow(e->type, g_crate_ptr->m_types.self(), e->lifetime);
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
                if (ivcr.is_valid_custom_receiver(inner)) {
                    arg_self_ty = g_crate_ptr->m_types.borrow(e->type, inner, e->lifetime);
                    receiver = ::HIR::Function::Receiver::Custom;
                }
            }
        } else if (const auto* e = arg_self_ty->opt_Path()) {
            // Box - Compare with `owned_box` lang item
            if (const auto* pe = e->path.m_data.opt_Generic()) {
                auto p = g_crate_ptr->get_lang_item_path_opt("owned_box");
                if (pe->m_path == p) {
                    if (pe->m_params.m_types.size() >= 1 && (pe->m_params.m_types[0] == g_crate_ptr->m_types.self() || pe->m_params.m_types[0] == real_self_type)) {
                        if (pe->m_params.m_types[0] == real_self_type) {
                            auto data = arg_self_ty->clone_data();
                            data.as_Path().path.m_data.as_Generic().m_params.m_types[0] = g_crate_ptr->m_types.self();
                            arg_self_ty = g_crate_ptr->m_types.intern(mv$(data));
                        }
                        receiver = ::HIR::Function::Receiver::Box;
                    }
                }
                // TODO: for other types, support arbitary structs/paths.
                if (receiver == ::HIR::Function::Receiver::Free) {
                    if (ivcr.is_valid_custom_receiver(arg_self_ty)) {
                        receiver = ::HIR::Function::Receiver::Custom;
                    }
                }
            }
        } else if (ivcr.is_valid_custom_receiver(arg_self_ty)) {
            receiver = ::HIR::Function::Receiver::Custom;
        } else {
        }

        if (receiver == ::HIR::Function::Receiver::Free) {
            ERROR(sp, E0000, "Unknown receiver type - " << arg_self_ty);
        }
    }

    bool force_emit = false;
    HIR::Function::Markings markings;
    switch (f.m_markings.inline_type) {
        case ::AST::Function::Markings::Inline::Auto:
            markings.inline_type = ::HIR::Function::Markings::Inline::Auto;
            break;
        case ::AST::Function::Markings::Inline::Never:
            markings.inline_type = ::HIR::Function::Markings::Inline::Never;
            break;
        case ::AST::Function::Markings::Inline::Always:
            markings.inline_type = ::HIR::Function::Markings::Inline::Always;
            force_emit = true;
            break;
        case ::AST::Function::Markings::Inline::Normal:
            markings.inline_type = ::HIR::Function::Markings::Inline::Normal;
            force_emit = true;
            break;
    }

    // #[rustc_legacy_const_generics] - Used to convert a literal argument into a const generic
    for (auto idx : f.m_markings.rustc_legacy_const_generics) {
        ASSERT_BUG(attrs.get("rustc_legacy_const_generics")->span(), idx < args.size() + f.m_markings.rustc_legacy_const_generics.size(), "#[rustc_legacy_const_generics(" << idx << ")] out of range (0.." << args.size() + f.m_markings.rustc_legacy_const_generics.size() << ")");
        markings.rustc_legacy_const_generics.push_back(idx);
    }
    // #[track_caller] - Provides caller information
    // NOTE: This can only be (cleanly) handled in the backend [where it sees fully monomorphised paths]
    if (attrs.get("track_caller")) {
        markings.track_caller = true;
    }
    markings.is_naked = f.m_markings.is_naked;

    ::HIR::Linkage linkage;
    switch (f.m_markings.linkage) {
        case AST::Linkage::Default:
            break;
        case AST::Linkage::Weak:
            linkage.type = HIR::Linkage::Type::Weak;
            break;
        case AST::Linkage::ExternWeak:
            BUG(sp, "Invalid linkage on function");
    }
    linkage.section = f.m_markings.link_section;

    // Convert #[link_name/no_mangle] attributes into the name
    if (g_ast_crate_ptr->m_test_harness && f.code().is_valid()) {
        // If we're making a test harness, and this item defines code, don't apply the linkage rules
    } else if (f.m_markings.link_name != "") {
        linkage.name = f.m_markings.link_name;
    } else if (attrs.get("rustc_std_internal_symbol")) {
        linkage.name = p.get_name();
        linkage.type = ::HIR::Linkage::Type::Weak;
    } else if (attrs.get("no_mangle")) {
        linkage.name = p.get_name();
    } else {
        // Leave linkage.name as empty
    }

    // If there's no code, mangle the name (According to the ABI) and set linkage.
    if (linkage.name == "" && !f.code().is_valid()) {
        linkage.name = p.get_name();
    }

    ::HIR::Function rv;
    rv.m_save_code = force_emit;
    rv.m_linkage = mv$(linkage);
    rv.m_receiver = receiver;
    if (receiver == HIR::Function::Receiver::Custom) {
        rv.m_receiver_type = MonomorphiserNop(g_crate_ptr->m_types).monomorph_type(f.args()[0].ty.span(), args.front().second, false);
        // Ensure that the reciever references `Self`
        ASSERT_BUG(
            f.args()[0].ty.span(),
            visit_ty_with(
                *rv.m_receiver_type,
                [](const HIR::TypeData* v) {
            return v->is_Generic() && v->as_Generic().is_self();
        }
            ),
            *rv.m_receiver_type
        );
    }
    rv.m_abi = RcString::new_interned(f.abi());
    rv.m_unsafe = f.is_unsafe();
    rv.m_const = f.is_const();
    rv.m_params = LowerHIR_GenericParams(f.params(), nullptr); // TODO: If this is a method, then it can add the Self: Sized bound
    rv.m_args = mv$(args);
    rv.m_variadic = f.is_variadic();
    rv.m_return = LowerHIR_Type(f.rettype());
    rv.m_code = LowerHIR_Expr(f.code());
    rv.m_markings = markings;

    if (f.is_async()) {
        //rv.m_markings.is_async = true;
        // Wrap the code in an async block
        if (rv.m_code) {
            auto* async_node = g_crate_ptr->m_pool->make<::HIR::ExprNode_AsyncBlock>(sp, rv.m_code.take_node(), true);
            async_node->m_res_type = g_crate_ptr->m_types.infer();
            rv.m_code = HIR::ExprPtr(::HIR::ExprNodeP(async_node));
        }
        // Make the return type be `impl Future<Output=Ret>`
        HIR::TraitPath future_path;
        future_path.m_path.m_path = g_crate_ptr->get_lang_item_path(sp, "future_trait");
        future_path.m_type_bounds.insert(std::make_pair(RcString::new_interned("Output"), ::HIR::TraitPath::AtyEqual{future_path.m_path.clone(), {}, std::move(rv.m_return)}));
        rv.m_return = g_crate_ptr->m_types.intern(::HIR::TypeData::make_ErasedType(::HIR::TypeData_ErasedType{true, ::make_vec1(std::move(future_path)), {}, ::HIR::TypeData_ErasedType_Inner::Data_Fcn{::HIR::Path(::HIR::SimplePath()), 0}}));
    }

    return rv;
}

void _add_mod_ns_item(::HIR::Module& mod, RcString name, ::HIR::Publicity is_pub, ::HIR::TypeItem ti) {
    mod.m_mod_items.insert(::std::make_pair(mv$(name), ::make_unique_ptr(::HIR::VisEnt<::HIR::TypeItem>{is_pub, mv$(ti)})));
}

void _add_mod_val_item(::HIR::Module& mod, RcString name, ::HIR::Publicity is_pub, ::HIR::ValueItem ti) {
    mod.m_value_items.insert(::std::make_pair(mv$(name), ::make_unique_ptr(::HIR::VisEnt<::HIR::ValueItem>{is_pub, mv$(ti)})));
}

void _add_mod_mac_item(::HIR::Module& mod, RcString name, ::HIR::Publicity is_pub, ::HIR::MacroItem ti) {
    mod.m_macro_items.insert(::std::make_pair(mv$(name), ::make_unique_ptr(::HIR::VisEnt<::HIR::MacroItem>{is_pub, mv$(ti)})));
}

::HIR::ValueItem LowerHIR_Static(::HIR::ItemPath p, const ::AST::AttributeList& attrs, const ::AST::Static& e, const Span& sp, const RcString& name) {
    TRACE_FUNCTION_F(p);

    if (e.s_class() == ::AST::Static::CONST) {
        // Note: Empty names are allowed for `const _: ...`
        return ::HIR::ValueItem::make_Constant(::HIR::Constant(::HIR::GenericParams{}, LowerHIR_Type(e.type()), LowerHIR_Expr(e.value())));
    } else {
        // Note: Empty names are allowed for `const _: ...`
        ASSERT_BUG(sp, name != "", "Empty constant name " << p);
        ::HIR::Linkage linkage;
        switch (e.m_markings.linkage) {
            case AST::Linkage::Default:
                break;
            case AST::Linkage::Weak:
                linkage.type = HIR::Linkage::Type::Weak;
                break;
            case AST::Linkage::ExternWeak:
                linkage.type = HIR::Linkage::Type::ExternWeak;
                break;
        }
        linkage.section = e.m_markings.link_section;

        if (e.m_markings.link_name != "") {
            linkage.name = e.m_markings.link_name;
        }
        // If there's no code, demangle the name (TODO: By ABI) and set linkage.
        else if (linkage.name == "" && !e.value().is_valid()) {
            linkage.name = name.c_str();
        }

        return ::HIR::ValueItem::make_Static(::HIR::Static(mv$(linkage), (e.s_class() == ::AST::Static::MUT), LowerHIR_Type(e.type()), LowerHIR_Expr(e.value())));
    }
}

::HIR::Module LowerHIR_Module(const ::AST::Module& ast_mod, ::HIR::ItemPath path, ::std::vector<::HIR::SimplePath> traits) {
    TRACE_FUNCTION_F("path = " << path);
    ::HIR::Module mod{};

    mod.m_traits = mv$(traits);

    auto mod_path = path.get_simple_path();
    auto get_vis = [&](const AST::Visibility& vis) {
        return LowerHIR_Vis(mod_path, vis);
    };

    // Populate trait list
    {
        struct Foo {
            HIR::Module& mod;

            Foo(HIR::Module& mod)
                : mod(mod)
            {
            }

            void push_trait(::HIR::SimplePath sp) {
                if (::std::find(mod.m_traits.begin(), mod.m_traits.end(), sp) == mod.m_traits.end()) {
                    mod.m_traits.push_back(mv$(sp));
                }
            }

            void push_trait_alias(const AST::PathBinding_Type::Data_TraitAlias& pbe) {
                if (pbe.trait_) {
                    push_trait_alias_ast(*pbe.trait_);
                } else if (pbe.hir) {
                    push_trait_alias_hir(*pbe.hir);
                } else {
                }
            }

            void push_trait_alias_ast(const AST::TraitAlias& ta) {
                for (const auto& e : ta.traits) {
                    if (const auto* pbe = e.ent.path->m_bindings.type.binding.opt_TraitAlias()) {
                        push_trait_alias(*pbe);
                    } else {
                        push_trait(LowerHIR_SimplePath(e.sp, *e.ent.path, FromAST_PathClass::Type, true));
                    }
                }
            }

            void push_trait_alias_hir(const HIR::TraitAlias& ta) {
                for (const auto& p : ta.m_traits) {
                    if (const auto* tap = g_crate_ptr->get_typeitem_by_path(Span(), p.m_path.m_path).opt_TraitAlias()) {
                        push_trait_alias_hir(*tap);
                    } else {
                        push_trait(p.m_path.m_path);
                    }
                }
            }
        };

        Foo f{mod};
        for (const auto& trait_path : ast_mod.m_traits) {
            f.push_trait(HIR::SimplePath((trait_path.crate == "" ? g_crate_name : trait_path.crate), trait_path.nodes));
        }
        for (const auto& i : ast_mod.m_type_items) {
            if (const auto* pbe = i.second.path.m_bindings.type.binding.opt_TraitAlias()) {
                // TODO: Should expanding trait aliases instead be handled in expr_cs__enum.cpp?
                f.push_trait_alias(*pbe);
            }
        }
    }

    for (unsigned int i = 0; i < ast_mod.anon_mods().size(); i++) {
        const auto& submod_ptr = ast_mod.anon_mods()[i];
        if (submod_ptr) {
            auto& submod = *submod_ptr;
            auto name = RcString::new_interned(FMT("#" << i));
            auto item_path = ::HIR::ItemPath(path, name.c_str());
            auto ti = ::HIR::TypeItem::make_Module(LowerHIR_Module(submod, item_path, mod.m_traits));
            _add_mod_ns_item(mod, mv$(name), ::HIR::Publicity::new_priv(mod_path), mv$(ti));
        }
    }

    for (const auto& ip : ast_mod.m_items) {
        const auto& item = *ip;
        const auto& sp = item.span;
        auto item_path = ::HIR::ItemPath(path, item.name.c_str());
        DEBUG(item_path << " " << item.data.tag_str());
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
                item.m_lines = std::move(e.lines);
                item.m_symbols.reserve(e.symbols.size());
                for (const AST::Path& s : e.symbols) {
                    item.m_symbols.push_back(LowerHIR_Path(Span(), s, FromAST_PathClass::Value));
                }
                item.m_options = e.options;
                g_crate_ptr->m_global_asm.push_back(std::move(item));
            }
            TU_ARMA(ExternBlock, e) {
                if (e.items().size() > 0) {
                    TODO(sp, "Expand ExternBlock");
                }
                for (const auto& lib : e.m_libraries) {
                    g_crate_ptr->m_ext_libs.push_back(::HIR::ExternLibrary{lib.lib_name});
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
                _add_mod_ns_item(mod, item.name, get_vis(item.vis), LowerHIR_Module(e, mv$(item_path)));
            }
            TU_ARMA(Crate, e) {
                // All 'extern crate' items should be normalised into a list in the crate root
                // - If public, add a namespace import here referring to the root of the imported crate
                _add_mod_ns_item(mod, item.name, get_vis(item.vis), ::HIR::TypeItem::make_Import({::HIR::SimplePath(e.name, {}), false, 0}));
            }
            TU_ARMA(Type, e) {
                if (e.type().m_data.is_Any()) {
                    if (!e.params().m_params.empty() || !e.params().m_bounds.empty()) {
                        ERROR(item.span, E0000, "Generics on extern type");
                    }
                    _add_mod_ns_item(mod, item.name, get_vis(item.vis), ::HIR::ExternType{});
                    break;
                }
                _add_mod_ns_item(mod, item.name, get_vis(item.vis), ::HIR::TypeItem::make_TypeAlias(LowerHIR_TypeAlias(item_path, e)));
            }
            TU_ARMA(Struct, e) {
                /// Add value reference
                if (e.m_data.is_Unit()) {
                    _add_mod_val_item(mod, item.name, get_vis(item.vis), ::HIR::ValueItem::make_StructConstant({item_path.get_simple_path()}));
                } else if (e.m_data.is_Tuple()) {
                    _add_mod_val_item(mod, item.name, get_vis(item.vis), ::HIR::ValueItem::make_StructConstructor({item_path.get_simple_path()}));
                } else {
                }
                _add_mod_ns_item(mod, item.name, get_vis(item.vis), LowerHIR_Struct(ip->span, item_path, e, item.attrs, mod));
            }
            TU_ARMA(Enum, e) {
                auto enm = LowerHIR_Enum(item_path, e, item.attrs, [&](auto name, auto str) {
                    _add_mod_ns_item(mod, name, get_vis(item.vis), mv$(str));
                }, mod);
                _add_mod_ns_item(mod, item.name, get_vis(item.vis), mv$(enm));
            }
            TU_ARMA(Union, e) {
                _add_mod_ns_item(mod, item.name, get_vis(item.vis), LowerHIR_Union(item_path, e, item.attrs));
            }
            TU_ARMA(Trait, e) {
                _add_mod_ns_item(mod, item.name, get_vis(item.vis), LowerHIR_Trait(item_path.get_simple_path(), e, item.attrs));
            }
            TU_ARMA(TraitAlias, e) {
                _add_mod_ns_item(mod, item.name, get_vis(item.vis), LowerHIR_TraitAlias(sp, item_path, e));
            }
            TU_ARMA(Function, e) {
                _add_mod_val_item(mod, item.name, get_vis(item.vis), LowerHIR_Function(item_path, item.attrs, e, ::HIR::TypeRef{}));
            }
            TU_ARMA(Static, e) {
                _add_mod_val_item(mod, item.name, get_vis(item.vis), LowerHIR_Static(item_path, item.attrs, e, sp, item.name));
            }
        }
    }
    // Some explicit handling of mac
    for (auto& mac : const_cast<AST::Module&>(ast_mod).macros()) {
        if (mac.data || mac.vis.is_global()) {
            ASSERT_BUG(mac.span, mac.data, "Null macro - " << mac.name);
            ASSERT_BUG(mac.span, mac.data->m_rules.size() > 0, "Empty macro - " << mac.name);
            _add_mod_mac_item(mod, mac.name, get_vis(mac.vis), std::move(mac.data));
        }
    }

    // Imports
    Span mod_span;
    for (const auto& ie : ast_mod.m_namespace_items) {
        const auto& sp = mod_span;
        // TODO: Only transfer private imports if this module contains a `macro`?
        // - Well... sub-modules that contain a `macro` would also lead to the same import
        if (ie.second.is_import) { //&& ie.second.is_pub ) {
            auto hir_path = LowerHIR_SimplePath(sp, ie.second.path, FromAST_PathClass::Type);
            assert(hir_path.components().empty() || hir_path.components().back() != "");
            ::HIR::TypeItem ti;
            if (const auto* pb = ie.second.path.m_bindings.type.binding.opt_EnumVar()) {
                DEBUG("Import NS " << ie.first << " = " << hir_path << " (Enum Variant)");
                ti = ::HIR::TypeItem::make_Import({mv$(hir_path), true, pb->idx});
            } else {
                DEBUG("Import NS " << ie.first << " = " << hir_path);
                ti = ::HIR::TypeItem::make_Import({mv$(hir_path), false, 0});
            }
            _add_mod_ns_item(mod, ie.first, get_vis(ie.second.vis), mv$(ti));
        }
    }
    for (const auto& ie : ast_mod.m_value_items) {
        const auto& sp = mod_span;
        // These have no purpose being in HIR (while unnamed traits can be used to bring traits into scope in downstream crates)
        if (ie.first.c_str()[0] == ' ') {
            continue;
        }
        // TODO: See code for `m_namespace_items` above
        if (ie.second.is_import) { //&& ie.second.is_pub ) {
            auto hir_path = LowerHIR_SimplePath(sp, ie.second.path, FromAST_PathClass::Value);
            assert(!hir_path.components().empty());
            assert(hir_path.components().back() != "");
            ::HIR::ValueItem vi;

            TU_MATCH_HDRA( (ie.second.path.m_bindings.value.binding), {)
            default:
                DEBUG("Import VAL " << ie.first << " = " << hir_path);
                vi = ::HIR::ValueItem::make_Import({mv$(hir_path), false, 0});
                TU_ARMA(EnumVar, pb) {
                    DEBUG("Import VAL " << ie.first << " = " << hir_path << " (Enum Variant)");
                    vi = ::HIR::ValueItem::make_Import({mv$(hir_path), true, pb.idx});
                }
            }
            _add_mod_val_item(mod, ie.first, get_vis(ie.second.vis), mv$(vi));
        }
    }

    for (const auto& ie : ast_mod.m_macro_items) {
        const auto& sp = mod_span;
        if (ie.first.c_str()[0] == ' ') {
            continue;
        }
        auto hir_path = LowerHIR_SimplePath(sp, ie.second.path, FromAST_PathClass::Macro);
        if (ie.second.is_import) {
            assert(!hir_path.components().empty());
            assert(hir_path.components().back() != "");

            DEBUG("Import MACRO " << ie.first << " = " << hir_path);
            auto mi = ::HIR::MacroItem::make_Import({mv$(hir_path)});
            _add_mod_mac_item(mod, ie.first, get_vis(ie.second.vis), mv$(mi));
        } else {
            DEBUG("Defined MACRO " << ie.first << " = " << hir_path);
        }
    }

    return mod;
}

void LowerHIR_Module_Impls(const ::AST::Module& ast_mod, ::HIR::Crate& hir_crate) {
    TRACE_FUNCTION_F(ast_mod.path());
    ::HIR::SimplePath mod_path(g_crate_name, ast_mod.path().nodes);

    // Sub-modules
    for (const auto& item : ast_mod.m_items) {
        if (const auto* e = item->data.opt_Module()) {
            LowerHIR_Module_Impls(*e, hir_crate);
        }
    }
    for (const auto& submod_ptr : ast_mod.anon_mods()) {
        if (submod_ptr) {
            LowerHIR_Module_Impls(*submod_ptr, hir_crate);
        }
    }

    //
    for (const auto& i : ast_mod.m_items) {
        if (!i->data.is_Impl()) {
            continue;
        }
        const auto& impl = i->data.as_Impl();
        const Span impl_span;
        auto params = LowerHIR_GenericParams(impl.def().params(), nullptr);

        TRACE_FUNCTION_F("IMPL " << impl.def());

        if (impl.def().trait().ent.is_valid()) {
            const auto& pb = impl.def().trait().ent.m_bindings.type.binding;
            ASSERT_BUG(Span(), pb.is_Trait(), "Binding for trait path in impl isn't a Trait - " << impl.def().trait().ent);
            ASSERT_BUG(Span(), pb.as_Trait().trait_ || pb.as_Trait().hir, "Trait pointer for trait path in impl isn't set");
            bool is_marker = (pb.as_Trait().trait_ ? pb.as_Trait().trait_->is_marker() : pb.as_Trait().hir->m_is_marker);
            auto trait_path = LowerHIR_GenericPath(impl.def().trait().sp, impl.def().trait().ent, FromAST_PathClass::Type);
            auto trait_name = mv$(trait_path.m_path);
            auto trait_args = mv$(trait_path.m_params);

            if (!is_marker) {
                auto type = LowerHIR_Type(impl.def().type());

                ::HIR::ItemPath path(type, trait_name, trait_args);
                DEBUG("path = " << path);

                ::std::map<RcString, ::HIR::TraitImpl::ImplEnt<::HIR::Function>> methods;
                ::std::map<RcString, ::HIR::TraitImpl::ImplEnt<::HIR::Constant>> constants;
                ::std::map<RcString, ::HIR::TraitImpl::ImplEnt<::HIR::TypeRef>> types;

                for (const auto& item : impl.items()) {
                    ::HIR::ItemPath item_path(path, item.name.c_str());
                    TU_MATCH_HDRA( (*item.data), {)
                    default:
                        BUG(item.sp, "Unexpected item type in trait impl - " << item.data->tag_str());
                        TU_ARMA(None, e) {
                        }
                        TU_ARMA(MacroInv, e) {
                        }
                        TU_ARMA(Static, e) {
                            if (e.s_class() == ::AST::Static::CONST) {
                                // TODO: Check signature against the trait?
                                constants.insert(::std::make_pair(item.name, ::HIR::TraitImpl::ImplEnt<::HIR::Constant>{item.is_specialisable, ::HIR::Constant(::HIR::GenericParams{}, LowerHIR_Type(e.type()), LowerHIR_Expr(e.value()))}));
                            } else {
                                TODO(item.sp, "Associated statics in trait impl");
                            }
                        }
                        TU_ARMA(Type, e) {
                            DEBUG("- type " << item.name);
                            auto aty_params = LowerHIR_GenericParams(e.params(), nullptr);
                            //ASSERT_BUG(Span(), aty_params.is_empty(), "TODO: GATs");

                            assert(!g_impl_trait_source.path);
                            HIR::ItemPath ip1(mod_path);
                            ::std::string name2 = ::std::string("#impl_") + ::std::to_string((uintptr_t)&impl) + "_" + item.name.c_str();
                            HIR::ItemPath ip2(ip1, name2.c_str());
                            g_impl_trait_source = ImplTraitSource(&ip2, &params, &aty_params);

                            types.insert(::std::make_pair(item.name, ::HIR::TraitImpl::ImplEnt<::HIR::TypeRef>{item.is_specialisable, LowerHIR_Type(e.type())}));

                            g_impl_trait_source = ImplTraitSource();
                        }
                        TU_ARMA(Function, e) {
                            DEBUG("- method " << item.name);
                            auto fcn = LowerHIR_Function(item_path, item.attrs, e, type);
                            if (impl.def().is_const()) {
                                fcn.m_const = true;
                            }
                            methods.insert(::std::make_pair(item.name, ::HIR::TraitImpl::ImplEnt<::HIR::Function>{item.is_specialisable, mv$(fcn)}));
                        }
                    }
                }

                // Sorted later on
                auto hir_impl = ::std::make_unique<HIR::TraitImpl>(::HIR::TraitImpl{
                        mv$(params),
                        mv$(trait_args),
                        mv$(type),

                        mv$(methods),
                        mv$(constants),
                        {}, // Statics
                        mv$(types),

                        mod_path
                    });
                hir_impl->m_is_const = impl.def().is_const();
                hir_crate.m_trait_impls[mv$(trait_name)].generic.push_back(mv$(hir_impl));
            } else if (impl.def().type().m_data.is_None()) {
                // Ignore - These are encoded in the 'is_marker' field of the trait
            } else {
                auto type = LowerHIR_Type(impl.def().type());
                hir_crate.m_marker_impls[mv$(trait_name)].generic.push_back(box$(
                    ::HIR::MarkerImpl{
                        mv$(params),
                        mv$(trait_args),
                        true,
                        mv$(type),

                        mod_path
                    }
                ));
            }
        } else {
            // Inherent impls
            auto type = LowerHIR_Type(impl.def().type());
            ::HIR::ItemPath path(type);

            auto get_vis = [&](const AST::Visibility& vis) {
                return LowerHIR_Vis(mod_path, vis);
            }; // TODO: Does this need to consume anon modules?

            ::std::map<RcString, ::HIR::TypeImpl::VisImplEnt<::HIR::Function>> methods;
            ::std::map<RcString, ::HIR::TypeImpl::VisImplEnt<::HIR::Constant>> constants;
            ::std::map<RcString, ::HIR::TypeImpl::VisImplEnt<::HIR::TypeAlias>> types;

            for (const auto& item : impl.items()) {
                ::HIR::ItemPath item_path(path, item.name.c_str());
                TU_MATCH_HDRA( (*item.data), {)
                default:
                    BUG(item.sp, "Unexpected item type in inherent impl - " << item.data->tag_str());
                    TU_ARMA(None, e) {
                    }
                    TU_ARMA(MacroInv, e) {
                    }
                    TU_ARMA(Static, e) {
                        if (e.s_class() == ::AST::Static::CONST) {
                            constants.insert(::std::make_pair(item.name, ::HIR::TypeImpl::VisImplEnt<::HIR::Constant>{get_vis(item.vis), item.is_specialisable, ::HIR::Constant(::HIR::GenericParams{}, LowerHIR_Type(e.type()), LowerHIR_Expr(e.value()))}));
                        } else {
                            TODO(item.sp, "Associated statics in inherent impl");
                        }
                    }
                    TU_ARMA(Type, e) {
                        DEBUG("- type " << item.name);
                        auto aty_params = LowerHIR_GenericParams(e.params(), nullptr);

                        assert(!g_impl_trait_source.path);
                        g_impl_trait_source = ImplTraitSource(&item_path, &params, &aty_params);
                        auto aty_type = LowerHIR_Type(e.type());
                        g_impl_trait_source = ImplTraitSource();

                        types.insert(::std::make_pair(item.name, ::HIR::TypeImpl::VisImplEnt<::HIR::TypeAlias>{
                            get_vis(item.vis),
                            item.is_specialisable,
                            ::HIR::TypeAlias{mv$(aty_params), mv$(aty_type)}
                        }));
                    }
                    TU_ARMA(Function, e) {
                        methods.insert(::std::make_pair(item.name, ::HIR::TypeImpl::VisImplEnt<::HIR::Function>{get_vis(item.vis), item.is_specialisable, LowerHIR_Function(item_path, item.attrs, e, type)}));
                    }
                }
            }

            // Sorted later on
            hir_crate.m_type_impls.generic.push_back(box$(
                ::HIR::TypeImpl{
                    mv$(params),
                    mv$(type),
                    mv$(methods),
                    mv$(constants),
                    mv$(types),

                    mod_path
                }
            ));
        }
    }
    for (const auto& i : ast_mod.m_items) {
        if (!i->data.is_NegImpl()) {
            continue;
        }
        const auto& impl = i->data.as_NegImpl();

        auto params = LowerHIR_GenericParams(impl.params(), nullptr);
        auto type = LowerHIR_Type(impl.type());
        auto trait = LowerHIR_GenericPath(impl.trait().sp, impl.trait().ent, FromAST_PathClass::Type);
        auto trait_name = mv$(trait.m_path);
        auto trait_args = mv$(trait.m_params);

        // Sorting done later
        hir_crate.m_marker_impls[mv$(trait_name)].generic.push_back(box$(
            ::HIR::MarkerImpl{
                mv$(params),
                mv$(trait_args),
                false,
                mv$(type),

                mod_path
            }
        ));
    }
}

class IndexVisitor: public ::HIR::Visitor {
    const ::HIR::Crate& crate;
    Span null_span;

public:
    IndexVisitor(const ::HIR::Crate& crate)
        : ::HIR::Visitor(nullptr, crate.m_types)
        , crate(crate)
    {
    }

    void visit_params(::HIR::GenericParams& params) override {
        for (auto& bound : params.m_bounds) {
            if (auto* e = bound.opt_TraitBound()) {
                e->trait.m_trait_ptr = &this->crate.get_trait_by_path(null_span, e->trait.m_path.m_path);
            }
        }
    }
};

/// \brief Converts the AST into HIR format
///
/// - Removes all possibility for unexpanded macros
/// - Performs desugaring of for/if-let/while-let/...
::HIR::Crate* LowerHIR_FromAST(stl::ObjPool* pool, ::AST::Crate& crate) {
    auto& rv = *pool->make<::HIR::Crate>(pool, crate.m_types);

    if (crate.m_crate_type != ::AST::Crate::Type::Executable) {
        rv.m_crate_name = crate.m_crate_name_real;
    } else {
        // Use a non-empty crate name that won't conflict with any libraries
        rv.m_crate_name = "bin#";
    }
    rv.m_edition = crate.m_edition;
    rv.m_is_no_core = crate.m_load_std == ::AST::Crate::LOAD_NONE;
    rv.m_no_main = crate.m_no_main;
    rv.m_features = crate.m_features;

    g_crate_ptr = &rv;
    g_ast_crate_ptr = &crate;
    g_crate_name = rv.m_crate_name;
    g_core_crate = crate.m_ext_cratename_core;
    auto macros = std::map<RcString, HIR::MacroItem>();
    //auto& macros = rv.m_exported_macros;

    // - Extract exported macros
    {
        TRACE_FUNCTION_FR("macros", "macros");
        ::std::vector<::AST::Module*> mods;
        mods.push_back(&crate.m_root_module);
        do {
            auto& mod = *mods.back();
            mods.pop_back();

            for (/*const*/ auto& mac : mod.macros()) {
                if (mac.data->m_exported) {
                    HIR::MacroItem mi;
                    if (&mod == &crate.m_root_module) {
                        mi = mv$(mac.data);
                    } else {
                        assert(mac.data);
                        assert(!mac.data->m_rules.empty());
                        auto pc = mod.path().nodes;
                        pc.push_back(mac.name);
                        mi = HIR::MacroItem::make_Import({::HIR::SimplePath(g_crate_name, std::move(pc))});
                    }
                    ASSERT_BUG(Span(), macros.count(mac.name) == 0, "Duplicate export of: " << mac.name);
                    if (macros.count(mac.name) == 0) {
                        auto res = macros.insert(::std::make_pair(mac.name, mv$(mi)));
                        if (res.second) {
                            DEBUG("- Define " << mac.name << "!");
                            rv.m_exported_macro_names.push_back(mac.name);
                        }
                        if (res.first->second.is_MacroRules()) {
                            ASSERT_BUG(Span(), !res.first->second.as_MacroRules()->m_rules.empty(), "Empty macro? - " << mac.name);
                        }
                    }

#if 1
                    for (auto& e : macros) {
                        if (e.second.is_MacroRules()) {
                            ASSERT_BUG(Span(), !e.second.as_MacroRules()->m_rules.empty(), "Empty macro? - " << e.first);
                        }
                    }
#endif
                } else {
                    DEBUG("- Non-exported " << mac.name << "!");
                }
            }

            for (auto& i : mod.m_items) {
                if (i->data.is_Module()) {
                    mods.push_back(&i->data.as_Module());
                }
            }
        } while (mods.size() > 0);

        for (const auto& mac : crate.m_root_module.m_macro_imports) {
            if (mac.is_pub || (mac.ref.is_MacroRules() && mac.ref.as_MacroRules()->m_exported)) {
                // Add to the re-export list
                auto path = ::HIR::SimplePath(mac.path.crate == "" ? g_crate_name : mac.path.crate, mac.path.nodes);
                auto res = macros.insert(std::make_pair(mac.name, HIR::MacroItem::make_Import({path})));
                if (!res.second) {
                    DEBUG("Conflict in imported vs local macros: " << mac.name);
                } else {
                    DEBUG("Re-export " << mac.name << "! = " << path);
                    rv.m_exported_macro_names.push_back(mac.name);
                }
            }
        }

        for (const auto& i : crate.m_root_module.m_macro_items) {
            if (i.second.vis.is_global()) {
                rv.m_exported_macro_names.push_back(i.first);
            }
        }
    }
    // - Proc Macros
    if (crate.m_crate_type == ::AST::Crate::Type::ProcMacro) {
        for (const auto& ent : crate.m_proc_macros) {
            struct H {
                static ::HIR::ProcMacro::Ty cvt_macro_ty(::AST::ProcMacroTy ast) {
                    switch (ast) {
                        case ::AST::ProcMacroTy::Function:
                            return ::HIR::ProcMacro::Ty::Function;
                        case ::AST::ProcMacroTy::Derive:
                            return ::HIR::ProcMacro::Ty::Derive;
                        case ::AST::ProcMacroTy::Attribute:
                            return ::HIR::ProcMacro::Ty::Attribute;
                    }
                    throw "Invalid AST macro type";
                }
            };

            // Register under an invalid SimplePath
            ::HIR::ProcMacro::Ty ty = H::cvt_macro_ty(ent.ty);
            macros.insert(std::make_pair(ent.name, ::HIR::ProcMacro{ty, ent.name, ::HIR::SimplePath(RcString(""), {ent.name}), ent.attributes}));
            rv.m_exported_macro_names.push_back(ent.name);
            DEBUG("Export proc_macro " << ent.name);
        }
    } else {
        ASSERT_BUG(Span(), crate.m_proc_macros.size() == 0, "Procedural macros defined in non proc-macro crate");
    }

    auto sp = Span();
    // - Store the lang item paths so conversion code can use them.
    for (const auto& lang_item_path : crate.m_lang_items) {
        assert(lang_item_path.second.crate == "");
        rv.m_lang_items.insert(::std::make_pair(lang_item_path.first, HIR::SimplePath(g_crate_name, lang_item_path.second.nodes)));
        DEBUG("Defined language item '" << lang_item_path.first << "' at " << lang_item_path.second);
    }
    rv.m_ext_crates_ordered = crate.m_extern_crates_ord;
    for (auto& ext_crate : crate.m_extern_crates) {
        // Populate m_lang_items from loaded crates too
        for (const auto& lang : ext_crate.second.m_hir->m_lang_items) {
            const auto& name = lang.first;
            const auto& path = lang.second;
            auto irv = rv.m_lang_items.insert(::std::make_pair(name, path));
            DEBUG("Load language item '" << lang.first << "' at " << lang.second << " from " << ext_crate.first);
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
        auto p1 = ext_crate.second.m_filename.rfind('/');
        auto p2 = ext_crate.second.m_filename.rfind('\\');
        auto p = (p1 == ::std::string::npos ? p2 : (p2 == ::std::string::npos ? p1 : ::std::max(p1, p2)));
        auto crate_file = (p == ::std::string::npos ? ext_crate.second.m_filename : ext_crate.second.m_filename.substr(p + 1));
        rv.m_ext_crates.insert(::std::make_pair(ext_crate.first, ::HIR::ExternCrate{ext_crate.second.m_hir, crate_file, ext_crate.second.m_filename}));
    }
    path_Sized = rv.get_lang_item_path_opt("sized");
    path_PointeeSized = rv.get_lang_item_path_opt("pointee_sized");
    path_MetadataSized = rv.get_lang_item_path_opt("metadata_sized");

    rv.m_root_module = LowerHIR_Module(crate.m_root_module, ::HIR::ItemPath(rv.m_crate_name));
    for (auto& e : macros) {
        if (e.second.is_MacroRules()) {
            ASSERT_BUG(Span(), !e.second.as_MacroRules()->m_rules.empty(), "Empty macro? - " << e.first);
        }
        rv.m_root_module.m_macro_items.insert(::std::make_pair(e.first, box$(HIR::VisEnt<HIR::MacroItem>{HIR::Publicity::new_global(), mv$(e.second)})));
    }

    LowerHIR_Module_Impls(crate.m_root_module, rv);

    // Set all pointers in the HIR to the correct (now fixed) locations
    //IndexVisitor(rv).visit_crate( rv );

    // Macro fixups:
    // - Convert interpolated AST items to token sequences
    {
        struct H {
            static void fix_macro_contents(std::vector<MacroExpansionEnt>& rule_contents) {
                for (auto it = rule_contents.begin(); it != rule_contents.end();) {
                    if (auto* tok = it->opt_Token()) {
                        //TODO: Can this share with `proc_macro`? Maybe a function on AST types to generate a token tree from the AST again.
                        struct NewToks {
                            std::vector<MacroExpansionEnt> out;

                            void emit_from_string(const std::string& s) {
                                ::std::istringstream iss{s};
                                Lexer l{iss, AST::Edition::Rust2021, {}};
                                for (;;) {
                                    auto t = l.getToken();
                                    if (t == TOK_EOF) {
                                        break;
                                    }
                                    out.push_back(t);
                                }
                            }

                            void emit_ast(const AST::ExprNode& e) {
                                if (const auto* ep = cast<const AST::ExprNode_Integer>(&e)) {
                                    out.push_back(Token(ep->m_value, ep->m_datatype));
                                } else if (const auto* ep = cast<const AST::ExprNode_Bool>(&e)) {
                                    out.push_back(ep->m_value ? TOK_RWORD_TRUE : TOK_RWORD_FALSE);
                                } else {
                                    throw std::runtime_error(FMT("Unknown node type: " << typeid(e).name()));
                                }
                            }

                            void emit_path(const ::AST::Path& path) {
                                ::std::stringstream ss;
                                ss << path;
                                emit_from_string(ss.str());
                            }

                            void emit_type(::TypeRef& ty) {
                                TU_MATCH_HDRA( (ty.m_data), { )
                                default:
                                    TODO(Span(), "Convert interpolated macro fragment: " << ty);
                                    TU_ARMA(Path, p) {
                                        emit_path(*p);
                                    }
                                }
                            }

                            void emit_tokentree(TokenTree& tt) {
                                if (tt.is_token()) {
                                    emit_token(tt.tok());
                                } else {
                                    for (size_t i = 0; i < tt.size(); i++) {
                                        emit_tokentree(tt[i]);
                                    }
                                }
                            }

                            void emit_token(Token& tok) {
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
                                        emit_type(tok.frag_type());
                                        break;
                                    case TOK_INTERPOLATED_META: {
                                        auto& i = tok.frag_meta();
                                        for (const auto& e : i.name().elems) {
                                            if (&e != &i.name().elems.front()) {
                                                out.push_back(Token(TOK_DOUBLE_COLON));
                                            }
                                            out.push_back(Token(TOK_IDENT, e));
                                        }
                                        emit_tokentree(i.data_mut());
                                        break;
                                    }
                                    case TOK_INTERPOLATED_EXPR:
                                        try {
                                            emit_ast(tok.frag_node());
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

                        NewToks new_toks;
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
                                new_toks.emit_token(*tok);
                                break;
                            default:
                                ++it;
                                continue;
                        }
                        if (new_toks.out.size() == 0) {
                            it = rule_contents.erase(it);
                        } else {
                            const auto replacement_count = new_toks.out.size();
                            *it = std::move(new_toks.out.front());
                            it += 1;
                            if (replacement_count > 1) {
                                it = rule_contents.insert(it, std::move_iterator<decltype(new_toks.out.begin())>(new_toks.out.begin() + 1), std::move_iterator<decltype(new_toks.out.begin())>(new_toks.out.end()));
                                it += replacement_count - 1;
                            }
                        }
                    } else {
                        ++it;
                    }
                }
            }

            static void fix_macros_in_mod(HIR::ItemPath path, HIR::Module& mod) {
                TRACE_FUNCTION_F(path);
                for (auto& mi : mod.m_mod_items) {
                    if (auto* submod_p = mi.second->ent.opt_Module()) {
                        fix_macros_in_mod(path + mi.first, *submod_p);
                    }
                }
                for (auto& mi : mod.m_macro_items) {
                    if (auto* mrpp = mi.second->ent.opt_MacroRules()) {
                        auto& mr = **mrpp;
                        if (mr.m_source_crate.size() == 0) {
                            mr.m_source_crate = g_crate_name;
                        }
                        for (auto& rule : mr.m_rules) {
                            fix_macro_contents(rule.m_contents);
                        }
                    }
                    if (const auto* i = mi.second->ent.opt_Import()) {
                        DEBUG(path << ": Import " << mi.first << " = " << i->path);
                        if (i->path.crate_name() == CRATE_BUILTINS) {
                        } else if (const auto* i2 = g_crate_ptr->get_macroitem_by_path(Span(), i->path).opt_Import()) {
                            BUG(Span(), "Attempted recusive import - " << i->path << " points at " << i2->path);
                        }
                    }
                }
            }
        };

        H::fix_macros_in_mod(HIR::ItemPath(""), rv.m_root_module);
    }

    if (g_core_crate == "") {
        g_core_crate = g_crate_name;
    }

    g_crate_ptr = nullptr;
    return &rv;
}


struct LowerHIR_ExprNode_Visitor: public ::AST::NodeVisitor {
    ::HIR::ExprNodeP m_rv;

    struct LoopLabel {
        Ident source;
        RcString lowered;
        size_t macro_definition_depth;
    };
    struct MacroDefinition {
        unsigned int definition_id;
        Ident::Hygiene token_hygiene;
        Ident::Hygiene definition_hygiene;
    };
    ::std::vector<LoopLabel> m_loop_labels;
    ::std::vector<MacroDefinition> m_macro_definitions;
    unsigned m_next_loop_label = 0;

    // Used to track if a closure is a generator or a normal closure
    // - They have different HIR node types
    bool m_has_yield = false;

    RcString enter_loop_label(const Ident& source) {
        if (source.name == "") {
            return {};
        }
        auto lowered = RcString::new_interned(FMT("@label" << m_next_loop_label++));
        m_loop_labels.push_back(LoopLabel{source, lowered, m_macro_definitions.size()});
        return lowered;
    }

    void leave_loop_label(const RcString& lowered) {
        if (lowered == "") {
            return;
        }
        assert(!m_loop_labels.empty());
        assert(m_loop_labels.back().lowered == lowered);
        m_loop_labels.pop_back();
    }

    RcString resolve_loop_label(const Span& sp, const Ident& target) const {
        if (target.name == "") {
            return {};
        }
        auto target_hygiene = target.hygiene;
        size_t definition_depth = m_macro_definitions.size();
        for (auto it = m_loop_labels.rbegin(); it != m_loop_labels.rend(); ++it) {
            while (definition_depth > it->macro_definition_depth) {
                const auto& definition = m_macro_definitions[--definition_depth];
                target_hygiene.leave_macro_definition(
                    definition.definition_id,
                    definition.token_hygiene,
                    definition.definition_hygiene
                );
            }
            if (it->source.name == target.name && it->source.hygiene.is_visible(target_hygiene)) {
                return it->lowered;
            }
        }
        ERROR(sp, E0000, "Could not find loop label '" << target.name);
    }

    ::HIR::ExprNodeP lower(::AST::ExprNodeP& ep) {
        assert(ep);
        ep->visit(*this);
        ASSERT_BUG(ep->span(), m_rv, ep.type_name() << " - Yielded a nullptr HIR node");
        m_rv->m_res_type = g_crate_ptr->m_types.infer();
        return std::move(m_rv);
    }

    ::HIR::ExprNodeP lower_opt(::AST::ExprNodeP& ep) {
        if (ep) {
            return lower(ep);
        } else {
            return nullptr;
        }
    }

    ::HIR::ExprNodeP lower_isolated(::AST::ExprNodeP& ep) {
        ::std::vector<LoopLabel> outer_labels;
        outer_labels.swap(m_loop_labels);
        auto rv = lower(ep);
        assert(m_loop_labels.empty());
        outer_labels.swap(m_loop_labels);
        return rv;
    }

    virtual void visit(::AST::ExprNode_Block& v) override {
        const size_t macro_definition_base = m_macro_definitions.size();
        auto label = enter_loop_label(v.m_label);
        auto rv = g_crate_ptr->m_pool->make<::HIR::ExprNode_Block>(v.span());
        bool last_has_semicolon = true;
        for (auto& n : v.m_nodes) {
            ASSERT_BUG(v.span(), n.node, "NULL node encountered in block");
            if (const auto* definition = cast<::AST::ExprNode_MacroDefinition>(n.node.get())) {
                m_macro_definitions.push_back(MacroDefinition{
                    definition->m_definition_id,
                    definition->m_token_hygiene,
                    definition->m_definition_hygiene
                });
                continue;
            }
            rv->m_nodes.push_back(lower(n.node));
            last_has_semicolon = n.has_semicolon;
        }
        leave_loop_label(label);
        // If the final node wasn't a statement (there wasn't a semicolon on it), then make that the value
        if (!rv->m_nodes.empty() && !last_has_semicolon) {
            rv->m_value_node = mv$(rv->m_nodes.back());
            rv->m_nodes.pop_back();
        }
        m_macro_definitions.resize(macro_definition_base);

        if (v.m_local_mod) {
            // TODO: Populate m_traits from the local module's import list
            rv->m_local_mod = ::HIR::SimplePath(g_crate_name, v.m_local_mod->path().nodes);
        }

        switch (v.m_block_type) {
            case AST::ExprNode_Block::Type::Bare:
                break;
            case AST::ExprNode_Block::Type::Unsafe:
                rv->m_is_unsafe = true;
                break;
            case AST::ExprNode_Block::Type::Const:
                break;
        }

        if (label != "") {
            if (rv->m_value_node) {
                auto* break_node = g_crate_ptr->m_pool->make<::HIR::ExprNode_LoopControl>(v.span(), label, /*cont=*/false, ::std::move(rv->m_value_node));
                rv->m_nodes.push_back(HIR::ExprNodeP(break_node));
                rv->m_value_node.reset();
            }
            auto* loop = g_crate_ptr->m_pool->make<::HIR::ExprNode_Loop>(v.span(), label, HIR::ExprNodeP(rv));
            loop->m_require_label = true;
            m_rv.reset(loop);
        } else {
            m_rv.reset(static_cast<::HIR::ExprNode*>(rv));
        }

        switch (v.m_block_type) {
            case AST::ExprNode_Block::Type::Bare:
                break;
            case AST::ExprNode_Block::Type::Unsafe:
                break;
            case AST::ExprNode_Block::Type::Const:
                m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_ConstBlock>(v.span(), std::move(m_rv)));
                break;
        }
    }

    virtual void visit(::AST::ExprNode_AsyncBlock& v) override {
        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_AsyncBlock>(v.span(), lower_isolated(v.m_inner), v.m_is_move));
    }

    virtual void visit(::AST::ExprNode_GeneratorBlock& v) override {
        // TODO: Wrap with something that provides an impl of Iterator
        // - `::core::iter::from_coroutine`
        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Generator>(
            v.span(),
            g_crate_ptr->m_types.infer(),
            g_crate_ptr->m_types.infer(),
            g_crate_ptr->m_types.infer(),
            lower_isolated(v.m_inner),
            v.m_is_move,
            false
        ));
        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_CallPath>(v.span(), HIR::SimplePath(g_core_crate, {"iter", "sources", "from_coroutine", "from_coroutine"}), make_vec1(mv$(m_rv))));
    }

    virtual void visit(::AST::ExprNode_Try& v) override {
        TODO(v.span(), "Handle _Try");
    }

    virtual void visit(::AST::ExprNode_Macro& v) override {
        BUG(v.span(), "Hit ExprNode_Macro");
    }

    virtual void visit(::AST::ExprNode_MacroDefinition& v) override {
        BUG(v.span(), "Hit ExprNode_MacroDefinition outside a block");
    }

    virtual void visit(::AST::ExprNode_Asm& v) override {
        ::std::vector<::HIR::ExprNode_Asm::ValRef> outputs;
        ::std::vector<::HIR::ExprNode_Asm::ValRef> inputs;
        for (auto& vr : v.m_output) {
            outputs.push_back(::HIR::ExprNode_Asm::ValRef{vr.name, lower(vr.value)});
        }
        for (auto& vr : v.m_input) {
            inputs.push_back(::HIR::ExprNode_Asm::ValRef{vr.name, lower(vr.value)});
        }

        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Asm>(v.span(), v.m_text, mv$(outputs), mv$(inputs), v.m_clobbers, v.m_flags));
    }

    virtual void visit(::AST::ExprNode_Asm2& v) override {
        std::vector<::HIR::ExprNode_Asm2::Param> params;
        for (auto& p : v.m_params) {
            TU_MATCH_HDRA((p), {)
            TU_ARMA(Const, e) {
                    ASSERT_BUG(v.span(), e, "Missing node for ASM Const");
                    params.push_back(lower(e));
                }
                TU_ARMA(Sym, e) {
                    params.push_back(LowerHIR_Path(v.span(), e, FromAST_PathClass::Value));
                }
                TU_ARMA(RegSingle, e) {
                    params.push_back(
                        ::HIR::ExprNode_Asm2::Param::make_RegSingle({
                            e.dir,
                            e.spec.clone(),
                            e.val ? lower(e.val) : nullptr // e.g. `lateout(regname) _`
                        })
                    );
                }
                TU_ARMA(Reg, e) {
                    params.push_back(::HIR::ExprNode_Asm2::Param::make_Reg({e.dir, e.spec.clone(), e.val_in ? lower(e.val_in) : nullptr, e.val_out ? lower(e.val_out) : nullptr}));
                }
            }
        }
        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Asm2>(v.span(), v.m_options, v.m_lines, mv$(params)));
    }

    virtual void visit(::AST::ExprNode_Flow& v) override {
        switch (v.m_type) {
            case ::AST::ExprNode_Flow::RETURN:
                if (v.m_value) {
                    m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Return>(v.span(), lower(v.m_value)));
                } else {
                    m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Return>(v.span(), ::HIR::ExprNodeP(g_crate_ptr->m_pool->make<::HIR::ExprNode_Tuple>(v.span(), ::std::vector<HIR::ExprNodeP>{}))));
                }
                break;
            case ::AST::ExprNode_Flow::YIELD:
                m_has_yield = true;
                {
                    auto value = v.m_value ? lower(v.m_value) : ::HIR::ExprNodeP(g_crate_ptr->m_pool->make<::HIR::ExprNode_Tuple>(v.span(), ::std::vector<HIR::ExprNodeP>{}));
                    m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Yield>(v.span(), std::move(value)));
                }
                break;
            case ::AST::ExprNode_Flow::CONTINUE:
            case ::AST::ExprNode_Flow::BREAK: {
                auto val = v.m_value ? lower(v.m_value) : ::HIR::ExprNodeP();
                ASSERT_BUG(v.span(), !(v.m_type == ::AST::ExprNode_Flow::CONTINUE && val), "Continue with a value isn't allowed");
                auto target = resolve_loop_label(v.span(), v.m_target);
                m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_LoopControl>(v.span(), mv$(target), (v.m_type == ::AST::ExprNode_Flow::CONTINUE), mv$(val)));
            } break;
            case ::AST::ExprNode_Flow::YEET:
                BUG(v.span(), "do yeet should have been desugared");
                break;
        }
    }

    virtual void visit(::AST::ExprNode_LetBinding& v) override {
        if (v.m_else) {
            // Cannot be expanded in expand, as it needs `None` to have been resolved to the enum variant
            // So, it's expanded here - with the cooperation of `Resolve_Absolute` allocating some variable bindings for us
            auto pat = LowerHIR_Pattern(v.m_pat);
            auto type = LowerHIR_Type(v.m_type);
            auto node_value = lower(v.m_value);
            auto node_else = lower(v.m_else);

            auto base = v.m_letelse_slots.first;
            auto count = v.m_letelse_slots.second;
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

                void visit_pattern(::HIR::Pattern& pat) override {
                    HIR::Visitor::visit_pattern(pat);
                    for (size_t i = 0; i < pat.m_bindings.size(); i++) {
                        this->handle_binding(pat.m_bindings[i]);
                    }
                    // SplitSlice also defines bindings
                    if (auto* e = pat.m_data.opt_SplitSlice()) {
                        if (e->extra_bind.is_valid()) {
                            this->handle_binding(e->extra_bind);
                        }
                    }
                    // - SplitTuple doesn't?
                    //if(auto* e = pat.m_data.opt_SplitTuple() ) {
                    //    if( e->extra_bind.is_valid() ) {
                    //        this->handle_binding(e->extra_bind);
                    //    }
                    //}
                }

                void handle_binding(::HIR::PatternBinding& pb) {
                    auto it = mapping.find(pb.m_slot);
                    if (it == mapping.end()) {
                        ASSERT_BUG(Span(), bindings.size() < this->count, "Miscount of variables in `let-else` - only allocated " << this->count);
                        unsigned new_idx = base + bindings.size();

                        bindings.push_back(HIR::PatternBinding(pb));
                        bindings.back().m_type = HIR::PatternBinding::Type::Move;
                        it = mapping.insert(std::make_pair(pb.m_slot, new_idx)).first;
                    }
                    pb.m_mutable = false;
                    pb.m_slot = it->second;
                }
            } visitor(g_crate_ptr->m_types, base, count);

            visitor.visit_pattern(pat);
            /*
             * ```
             * let (a,b,c,...) = match $value: $ty {
             *     $pat => (a,b,c,...),
             *     _ => { let _: ! = $else; },
             *     };
             * ```
             */
            std::vector<HIR::Pattern> new_pats;
            std::vector<HIR::ExprNodeP> tuple_vals;
            const auto binding_slots = HIR::pattern_binding_slots(pat, HIR::PatternBindingOrder::FirstCandidate);
            ASSERT_BUG(v.span(), binding_slots.size() == visitor.bindings.size(), "let-else candidate omitted bindings");
            for (const auto slot : binding_slots) {
                ASSERT_BUG(v.span(), base <= slot && slot - base < visitor.bindings.size(), "Invalid temporary let-else binding " << slot);
                auto& binding = visitor.bindings[slot - base];
                tuple_vals.push_back(HIR::ExprNodeP(g_crate_ptr->m_pool->make<HIR::ExprNode_Variable>(v.span(), binding.m_name, slot)));
                new_pats.push_back(HIR::Pattern(std::move(binding), HIR::Pattern::Data{}));
            }

            std::vector<HIR::ExprNode_Match::Arm> match_arms(2);
            // `$pat => (a,b,c,...),`
            match_arms[0].m_patterns.push_back(std::move(pat));
            match_arms[0].m_code.reset(g_crate_ptr->m_pool->make<HIR::ExprNode_Tuple>(v.span(), std::move(tuple_vals)));
            match_arms[1].m_patterns.push_back(HIR::Pattern());
            // `_ => loop { let _: ! = $else; },
            match_arms[1].m_code.reset(g_crate_ptr->m_pool->make<HIR::ExprNode_Let>(v.span(), HIR::Pattern(), g_crate_ptr->m_types.diverge(), std::move(node_else)));
            match_arms[1].m_code.reset(g_crate_ptr->m_pool->make<HIR::ExprNode_Loop>(v.span(), "", std::move(match_arms[1].m_code), /*require_label*/ true));
            // HACK: Just use the code as-is.
            //match_arms[1].m_code = std::move(node_else);
            // `match $value: $ty {`
            auto match_value = type->is_Infer() // Only emit the `: $ty` part if the type was specified (not a `_`)
                                   ? std::move(node_value)
                                   : HIR::ExprNodeP(g_crate_ptr->m_pool->make<HIR::ExprNode_Unsize>(v.span(), std::move(node_value), std::move(type)));
            auto match = HIR::ExprNodeP(g_crate_ptr->m_pool->make<HIR::ExprNode_Match>(v.span(), std::move(match_value), std::move(match_arms), true));

            // `let (a,b,c,...) = ...`
            m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Let>(v.span(), HIR::Pattern(::std::vector<HIR::PatternBinding>(), HIR::Pattern::Data::make_Tuple({std::move(new_pats)})), g_crate_ptr->m_types.infer(), std::move(match)));
        } else {
            m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Let>(v.span(), LowerHIR_Pattern(v.m_pat), LowerHIR_Type(v.m_type), lower_opt(v.m_value), v.m_is_super));
        }
    }

    virtual void visit(::AST::ExprNode_Assign& v) override {
        struct H {
            static ::HIR::ExprNode_Assign::Op get_op(::AST::ExprNode_Assign::Operation o) {
                switch (o) {
                    case ::AST::ExprNode_Assign::NONE:
                        return ::HIR::ExprNode_Assign::Op::None;
                    case ::AST::ExprNode_Assign::ADD:
                        return ::HIR::ExprNode_Assign::Op::Add;
                    case ::AST::ExprNode_Assign::SUB:
                        return ::HIR::ExprNode_Assign::Op::Sub;

                    case ::AST::ExprNode_Assign::MUL:
                        return ::HIR::ExprNode_Assign::Op::Mul;
                    case ::AST::ExprNode_Assign::DIV:
                        return ::HIR::ExprNode_Assign::Op::Div;
                    case ::AST::ExprNode_Assign::MOD:
                        return ::HIR::ExprNode_Assign::Op::Mod;

                    case ::AST::ExprNode_Assign::AND:
                        return ::HIR::ExprNode_Assign::Op::And;
                    case ::AST::ExprNode_Assign::OR:
                        return ::HIR::ExprNode_Assign::Op::Or;
                    case ::AST::ExprNode_Assign::XOR:
                        return ::HIR::ExprNode_Assign::Op::Xor;

                    case ::AST::ExprNode_Assign::SHR:
                        return ::HIR::ExprNode_Assign::Op::Shr;
                    case ::AST::ExprNode_Assign::SHL:
                        return ::HIR::ExprNode_Assign::Op::Shl;
                }
                throw "";
            }
        };

        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Assign>(v.span(), H::get_op(v.m_op), lower(v.m_slot), lower(v.m_value)));
    }

    virtual void visit(::AST::ExprNode_BinOp& v) override {
        ::HIR::ExprNode_BinOp::Op op;
        switch (v.m_type) {
            case ::AST::ExprNode_BinOp::RANGE: {
                BUG(v.span(), "Unexpected RANGE binop");
                break;
            }
            case ::AST::ExprNode_BinOp::RANGE_INC: {
                BUG(v.span(), "Unexpected RANGE_INC binop");
                break;
            }
            case ::AST::ExprNode_BinOp::PLACE_IN:
                m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Emplace>(v.span(), ::HIR::ExprNode_Emplace::Type::Placer, lower(v.m_left), lower(v.m_right)));
                break;

            case ::AST::ExprNode_BinOp::CMPEQU:
                op = ::HIR::ExprNode_BinOp::Op::CmpEqu;
                if (0) {
                    case ::AST::ExprNode_BinOp::CMPNEQU:
                        op = ::HIR::ExprNode_BinOp::Op::CmpNEqu;
                }
                if (0) {
                    case ::AST::ExprNode_BinOp::CMPLT:
                        op = ::HIR::ExprNode_BinOp::Op::CmpLt;
                }
                if (0) {
                    case ::AST::ExprNode_BinOp::CMPLTE:
                        op = ::HIR::ExprNode_BinOp::Op::CmpLtE;
                }
                if (0) {
                    case ::AST::ExprNode_BinOp::CMPGT:
                        op = ::HIR::ExprNode_BinOp::Op::CmpGt;
                }
                if (0) {
                    case ::AST::ExprNode_BinOp::CMPGTE:
                        op = ::HIR::ExprNode_BinOp::Op::CmpGtE;
                }
                if (0) {
                    case ::AST::ExprNode_BinOp::BOOLAND:
                        op = ::HIR::ExprNode_BinOp::Op::BoolAnd;
                }
                if (0) {
                    case ::AST::ExprNode_BinOp::BOOLOR:
                        op = ::HIR::ExprNode_BinOp::Op::BoolOr;
                }
                if (0) {
                    case ::AST::ExprNode_BinOp::BITAND:
                        op = ::HIR::ExprNode_BinOp::Op::And;
                }
                if (0) {
                    case ::AST::ExprNode_BinOp::BITOR:
                        op = ::HIR::ExprNode_BinOp::Op::Or;
                }
                if (0) {
                    case ::AST::ExprNode_BinOp::BITXOR:
                        op = ::HIR::ExprNode_BinOp::Op::Xor;
                }
                if (0) {
                    case ::AST::ExprNode_BinOp::MULTIPLY:
                        op = ::HIR::ExprNode_BinOp::Op::Mul;
                }
                if (0) {
                    case ::AST::ExprNode_BinOp::DIVIDE:
                        op = ::HIR::ExprNode_BinOp::Op::Div;
                }
                if (0) {
                    case ::AST::ExprNode_BinOp::MODULO:
                        op = ::HIR::ExprNode_BinOp::Op::Mod;
                }
                if (0) {
                    case ::AST::ExprNode_BinOp::ADD:
                        op = ::HIR::ExprNode_BinOp::Op::Add;
                }
                if (0) {
                    case ::AST::ExprNode_BinOp::SUB:
                        op = ::HIR::ExprNode_BinOp::Op::Sub;
                }
                if (0) {
                    case ::AST::ExprNode_BinOp::SHR:
                        op = ::HIR::ExprNode_BinOp::Op::Shr;
                }
                if (0) {
                    case ::AST::ExprNode_BinOp::SHL:
                        op = ::HIR::ExprNode_BinOp::Op::Shl;
                }

                m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_BinOp>(v.span(), op, lower(v.m_left), lower(v.m_right)));
                break;
        }
    }

    virtual void visit(::AST::ExprNode_UniOp& v) override {
        ::HIR::ExprNode_UniOp::Op op;
        switch (v.m_type) {
            case ::AST::ExprNode_UniOp::BOX: {
                m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Emplace>(v.span(), ::HIR::ExprNode_Emplace::Type::Boxer, ::HIR::ExprNodeP(g_crate_ptr->m_pool->make<::HIR::ExprNode_Tuple>(v.span(), ::std::vector<HIR::ExprNodeP>{})), lower(v.m_value)));
            } break;
            case ::AST::ExprNode_UniOp::QMARK:
                BUG(v.span(), "Encounterd question mark operator (should have been expanded in AST)");
                break;

            case ::AST::ExprNode_UniOp::REF:
                m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Borrow>(v.span(), ::HIR::BorrowType::Shared, lower(v.m_value)));
                break;
            case ::AST::ExprNode_UniOp::RawBorrow:
                m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_RawBorrow>(v.span(), ::HIR::BorrowType::Shared, lower(v.m_value)));
                break;
            case ::AST::ExprNode_UniOp::REFMUT:
                m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Borrow>(v.span(), ::HIR::BorrowType::Unique, lower(v.m_value)));
                break;
            case ::AST::ExprNode_UniOp::RawBorrowMut:
                m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_RawBorrow>(v.span(), ::HIR::BorrowType::Unique, lower(v.m_value)));
                break;

            case ::AST::ExprNode_UniOp::AWait:
                m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_AWait>(v.span(), lower(v.m_value)));
                break;

            case ::AST::ExprNode_UniOp::INVERT:
                op = ::HIR::ExprNode_UniOp::Op::Invert;
                if (0) {
                    case ::AST::ExprNode_UniOp::NEGATE:
                        op = ::HIR::ExprNode_UniOp::Op::Negate;
                }
                m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_UniOp>(v.span(), op, lower(v.m_value)));
                break;
        }
    }

    virtual void visit(::AST::ExprNode_Cast& v) override {
        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Cast>(v.span(), lower(v.m_value), LowerHIR_Type(v.m_type)));
    }

    virtual void visit(::AST::ExprNode_TypeAnnotation& v) override {
        // TODO: Create a proper node for this
        // - Using `Unsize` works pretty well, but isn't quite "correct"
        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Unsize>(v.span(), lower(v.m_value), LowerHIR_Type(v.m_type)));
    }

    virtual void visit(::AST::ExprNode_CallPath& v) override {
        ::std::vector<::HIR::ExprNodeP> args;
        for (auto& arg : v.m_args) {
            args.push_back(lower(arg));
        }

        if (const auto* e = v.m_path.m_class.opt_Local()) {
            m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_CallValue>(v.span(), ::HIR::ExprNodeP(g_crate_ptr->m_pool->make<::HIR::ExprNode_Variable>(v.span(), e->name, v.m_path.m_bindings.value.binding.as_Variable().slot)), mv$(args)));
        } else {
            TU_MATCH_HDRA( (v.m_path.m_bindings.value.binding), {)
            default:
                m_rv.reset( g_crate_ptr->m_pool->make<::HIR::ExprNode_CallPath>( v.span(),
                    LowerHIR_Path(v.span(), v.m_path, FromAST_PathClass::Value),
                    mv$( args )
                    ) );
                TU_ARMA(Static, e) {
                    bool is_const = e.static_ ? e.static_->s_class() == ::AST::Static::Class::CONST : (e.hir ? false : true) // If HIR Pointer is null, this is a HIR::Const
                        ;
                    m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_CallValue>(v.span(), ::HIR::ExprNodeP(g_crate_ptr->m_pool->make<::HIR::ExprNode_PathValue>(v.span(), LowerHIR_Path(v.span(), v.m_path, FromAST_PathClass::Value), is_const ? ::HIR::ExprNode_PathValue::CONSTANT : ::HIR::ExprNode_PathValue::STATIC)), mv$(args)));
                }
                //TU_ARMA(TypeAlias, e) {
                //    TODO(v.span(), "CallPath -> TupleVariant TypeAlias");
                //    }
                TU_ARMA(EnumVar, e) {
                    m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_TupleVariant>(v.span(), LowerHIR_GenericPath(v.span(), v.m_path, FromAST_PathClass::Value), false, mv$(args)));
                }
                TU_ARMA(Struct, e) {
                    m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_TupleVariant>(v.span(), LowerHIR_GenericPath(v.span(), v.m_path, FromAST_PathClass::Value), true, mv$(args)));
                }
            }
        }
    }

    virtual void visit(::AST::ExprNode_CallMethod& v) override {
        ::std::vector<::HIR::ExprNodeP> args;
        for (auto& arg : v.m_args) {
            args.push_back(lower(arg));
        }

        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_CallMethod>(v.span(), lower(v.m_val), v.m_method.name(), LowerHIR_PathParams(v.span(), v.m_method.args(), /*allow_assoc=*/false), mv$(args)));
    }

    virtual void visit(::AST::ExprNode_CallObject& v) override {
        ::std::vector<::HIR::ExprNodeP> args;
        for (auto& arg : v.m_args) {
            args.push_back(lower(arg));
        }

        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_CallValue>(v.span(), lower(v.m_val), mv$(args)));
    }

    virtual void visit(::AST::ExprNode_Loop& v) override {
        auto label = enter_loop_label(v.m_label);
        auto code = lower(v.m_code);
        leave_loop_label(label);
        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Loop>(v.span(), mv$(label), mv$(code)));
    }

    void visit(::AST::ExprNode_For& v) override {
        // NOTE: This should already be desugared (as a pass before resolve)
        BUG(v.span(), "Encountered still-sugared for loop");
    }

    ::std::vector<::HIR::ExprNode_Match::Guard> iflet_to_guards(std::vector<AST::IfLet_Condition>& guards) {
        ::std::vector<::HIR::ExprNode_Match::Guard> rv;
        rv.reserve(guards.size());
        for (auto& c : guards) {
            auto cond_pat = c.opt_pat ? LowerHIR_Pattern(*c.opt_pat) : HIR::Pattern{HIR::PatternBinding(), HIR::Pattern::Data::make_Value({::HIR::Pattern::Value::make_Integer({HIR::CoreType::Bool, U128(1)})})};
            auto cond_val = lower_opt(c.value);
            rv.push_back(::HIR::ExprNode_Match::Guard{std::move(cond_pat), std::move(cond_val), c.opt_pat ? false : true});
        }
        return rv;
    }

    virtual void visit(::AST::ExprNode_While& v) override {
        // Desugar to `loop { match () { _ if ... => { body }, _ => break, } }`
        auto label = enter_loop_label(v.m_label);
        ::std::vector<::HIR::ExprNode_Match::Arm> arms;
        arms.push_back(::HIR::ExprNode_Match::Arm{make_vec1(::HIR::Pattern()), iflet_to_guards(v.m_conditions), lower(v.m_code)});
        arms.push_back(::HIR::ExprNode_Match::Arm{make_vec1(::HIR::Pattern()), {}, HIR::ExprNodeP(g_crate_ptr->m_pool->make<HIR::ExprNode_LoopControl>(v.span(), "", false, nullptr))});
        leave_loop_label(label);
        m_rv.reset(g_crate_ptr->m_pool->make<HIR::ExprNode_Loop>(v.span(), mv$(label), HIR::ExprNodeP(g_crate_ptr->m_pool->make<HIR::ExprNode_Match>(v.span(), HIR::ExprNodeP(g_crate_ptr->m_pool->make<HIR::ExprNode_Tuple>(v.span(), ::std::vector<HIR::ExprNodeP>())), std::move(arms)))));
    }

    virtual void visit(::AST::ExprNode_Match& v) override {
        ::std::vector<::HIR::ExprNode_Match::Arm> arms;

        for (auto& arm : v.m_arms) {
            ::HIR::ExprNode_Match::Arm new_arm{{}, iflet_to_guards(arm.m_guard), lower(arm.m_code)};

            for (const auto& pat : arm.m_patterns) {
                new_arm.m_patterns.push_back(LowerHIR_Pattern(pat));
            }

            arms.push_back(mv$(new_arm));
        }

        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Match>(v.span(), lower(v.m_val), mv$(arms)));
    }

    virtual void visit(::AST::ExprNode_If& v) override {
        ::std::vector<::HIR::ExprNode_Match::Arm> arms;
        // Desugar to a `match`
        for (auto& arm : v.m_arms) {
            arms.push_back(::HIR::ExprNode_Match::Arm{make_vec1(::HIR::Pattern()), iflet_to_guards(arm.m_conditions), lower(arm.m_body)});
        }
        arms.push_back(::HIR::ExprNode_Match::Arm{make_vec1(::HIR::Pattern()), {}, v.m_else ? lower(v.m_else) : HIR::ExprNodeP(g_crate_ptr->m_pool->make<HIR::ExprNode_Tuple>(v.span(), ::std::vector<HIR::ExprNodeP>()))});

        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Match>(v.span(), HIR::ExprNodeP(g_crate_ptr->m_pool->make<HIR::ExprNode_Tuple>(v.span(), ::std::vector<HIR::ExprNodeP>())), std::move(arms)));
    }

    virtual void visit(::AST::ExprNode_WildcardPattern& v) override {
        ERROR(v.span(), E0000, "`_` is only valid in expressions on the left-hand side of an assignment");
    }

    virtual void visit(::AST::ExprNode_Integer& v) override {
        struct H {
            static ::HIR::CoreType get_type(Span sp, ::eCoreType ct) {
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
                        BUG(sp, "Unknown type for integer literal - " << coretype_name(ct));
                }
            }
        };

        if (v.m_datatype == CORETYPE_F32 || v.m_datatype == CORETYPE_F64) {
            DEBUG("Integer annotated as float, create float node");
            m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Literal>(v.span(), ::HIR::ExprNode_Literal::Data::make_Float({(v.m_datatype == CORETYPE_F32 ? ::HIR::CoreType::F32 : ::HIR::CoreType::F64), v.m_value.to_double()})));
            return;
        }
        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Literal>(v.span(), ::HIR::ExprNode_Literal::Data::make_Integer({H::get_type(v.span(), v.m_datatype), v.m_value})));
    }

    virtual void visit(::AST::ExprNode_Float& v) override {
        ::HIR::CoreType ct;
        switch (v.m_datatype) {
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
                BUG(v.span(), "Unknown type for float literal - " << coretype_name(v.m_datatype));
        }
        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Literal>(v.span(), ::HIR::ExprNode_Literal::Data::make_Float({ct, v.m_value})));
    }

    virtual void visit(::AST::ExprNode_Bool& v) override {
        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Literal>(v.span(), ::HIR::ExprNode_Literal::Data::make_Boolean(v.m_value)));
    }

    virtual void visit(::AST::ExprNode_String& v) override {
        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Literal>(v.span(), ::HIR::ExprNode_Literal::Data::make_String(v.m_value)));
    }

    virtual void visit(::AST::ExprNode_ByteString& v) override {
        ::std::vector<char> dat{v.m_value.begin(), v.m_value.end()};
        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Literal>(v.span(), ::HIR::ExprNode_Literal::Data::make_ByteString(mv$(dat))));
    }

    virtual void visit(::AST::ExprNode_CString& v) override {
        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Literal>(v.span(), ::HIR::ExprNode_Literal::Data::make_CString({v.m_value})));
    }

    virtual void visit(::AST::ExprNode_Closure& v) override {
        ::HIR::ExprNode_Closure::args_t args;
        for (const auto& arg : v.m_args) {
            args.push_back(::std::make_pair(LowerHIR_Pattern(arg.first), LowerHIR_Type(arg.second)));
        }

        auto orig_has_yield = m_has_yield;
        m_has_yield = false;
        auto inner = lower_isolated(v.m_code);
        auto has_yield = m_has_yield;
        m_has_yield = orig_has_yield;

        if (has_yield) {
            // NOTE: One argument could be present with yielding arguments?
            if (!args.empty()) {
                ERROR(v.span(), E0000, "Generator closures don't take arguments.");
            }
            m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Generator>(
                v.span(),
                LowerHIR_Type(v.m_return),
                g_crate_ptr->m_types.infer(),
                g_crate_ptr->m_types.infer(),
                mv$(inner),
                v.m_is_move,
                v.m_is_pinned
            ));
        } else {
            if (v.m_is_pinned) {
                ERROR(v.span(), E0000, "Invalid use of `static` on non-yielding closure");
            }
            m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Closure>(v.span(), std::move(args), LowerHIR_Type(v.m_return), std::move(inner), v.m_is_move));
        }
    }

    virtual void visit(::AST::ExprNode_StructLiteral& v) override {
        if (v.m_path.m_bindings.type.binding.is_Union()) {
            if (v.m_values.size() != 1) {
                ERROR(v.span(), E0000, "Union constructors can only specify a single field");
            }
            if (v.m_base_value) {
                ERROR(v.span(), E0000, "Union constructors can't take a base value");
            }
        }

        ::HIR::ExprNode_StructLiteral::t_values values;
        for (auto& val : v.m_values) {
            values.push_back(::std::make_pair(val.name, lower(val.value)));
        }

        if (values.empty() && !v.m_base_value) {
            enum class EmptyKind {
                None,
                Unit,
                Tuple,
            };

            if (const auto* binding = v.m_path.m_bindings.type.binding.opt_EnumVar()) {
                EmptyKind kind = EmptyKind::None;
                if (binding->enum_) {
                    const auto& data = binding->enum_->variants().at(binding->idx).m_data;
                    kind = data.is_Unit() ? EmptyKind::Unit
                        : data.is_Tuple() && data.as_Tuple().m_items.empty() ? EmptyKind::Tuple
                        : EmptyKind::None;
                } else if (binding->hir) {
                    const auto& enm = *binding->hir;
                    if (enm.m_data.is_Value()) {
                        kind = EmptyKind::Unit;
                    } else {
                        const auto& var = enm.m_data.as_Data().at(binding->idx);
                        if (var.type == g_crate_ptr->m_types.unit()) {
                            kind = EmptyKind::Unit;
                        } else {
                            const auto& str = *var.type->as_Path().binding.as_Struct();
                            kind = str.m_data.is_Unit() ? EmptyKind::Unit
                                : str.m_data.is_Tuple() && str.m_data.as_Tuple().empty() ? EmptyKind::Tuple
                                : EmptyKind::None;
                        }
                    }
                }
                if (kind == EmptyKind::Unit) {
                    m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_UnitVariant>(
                        v.span(), LowerHIR_GenericPath(v.span(), v.m_path, FromAST_PathClass::Type), false));
                    return;
                }
                if (kind == EmptyKind::Tuple) {
                    m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_TupleVariant>(
                        v.span(), LowerHIR_GenericPath(v.span(), v.m_path, FromAST_PathClass::Type), false, ::std::vector<::HIR::ExprNodeP>{}));
                    return;
                }
            } else if (const auto* binding = v.m_path.m_bindings.type.binding.opt_Struct()) {
                EmptyKind kind = EmptyKind::None;
                if (binding->struct_) {
                    const auto& data = binding->struct_->m_data;
                    kind = data.is_Unit() ? EmptyKind::Unit
                        : data.is_Tuple() && data.as_Tuple().ents.empty() ? EmptyKind::Tuple
                        : EmptyKind::None;
                } else if (binding->hir) {
                    const auto& data = binding->hir->m_data;
                    kind = data.is_Unit() ? EmptyKind::Unit
                        : data.is_Tuple() && data.as_Tuple().empty() ? EmptyKind::Tuple
                        : EmptyKind::None;
                }
                if (kind == EmptyKind::Unit) {
                    m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_UnitVariant>(
                        v.span(), LowerHIR_GenericPath(v.span(), v.m_path, FromAST_PathClass::Type), true));
                    return;
                }
                if (kind == EmptyKind::Tuple) {
                    m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_TupleVariant>(
                        v.span(), LowerHIR_GenericPath(v.span(), v.m_path, FromAST_PathClass::Type), true, ::std::vector<::HIR::ExprNodeP>{}));
                    return;
                }
            }
        }
        auto ty = LowerHIR_Type(::TypeRef(v.span(), v.m_path));
        if (v.m_path.m_bindings.type.binding.is_EnumVar()) {
            ASSERT_BUG(v.span(), TU_TEST1(*ty, Path, .path.m_data.is_Generic()), "Enum variant path not GenericPath: " << ty);
            auto data = ty->clone_data();
            auto& gp = data.as_Path().path.m_data.as_Generic();
            auto var_name = gp.m_path.pop_component();
            auto enum_ty = g_crate_ptr->m_types.intern(mv$(data));
            ty = g_crate_ptr->m_types.path(::HIR::Path(enum_ty, mv$(var_name)), {});
        }
        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_StructLiteral>(v.span(), mv$(ty), !v.m_path.m_bindings.type.binding.is_EnumVar(), lower_opt(v.m_base_value), mv$(values)));
    }

    virtual void visit(::AST::ExprNode_StructLiteralPattern& v) override {
        if (v.m_path.m_bindings.type.binding.is_Union()) {
            if (v.m_values.size() != 1) {
                ERROR(v.span(), E0000, "Union constructors can only specify a single field");
            }
        }

        ::HIR::ExprNode_StructLiteral::t_values values;
        for (auto& val : v.m_values) {
            values.push_back(::std::make_pair(val.name, lower(val.value)));
        }
        auto ty = LowerHIR_Type(::TypeRef(v.span(), v.m_path));
        if (v.m_path.m_bindings.type.binding.is_EnumVar()) {
            ASSERT_BUG(v.span(), TU_TEST1(*ty, Path, .path.m_data.is_Generic()), "Enum variant path not GenericPath: " << ty);
            auto data = ty->clone_data();
            auto& gp = data.as_Path().path.m_data.as_Generic();
            auto var_name = gp.m_path.pop_component();
            auto enum_ty = g_crate_ptr->m_types.intern(mv$(data));
            ty = g_crate_ptr->m_types.path(::HIR::Path(enum_ty, mv$(var_name)), {});
        }
        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_StructLiteral>(v.span(), mv$(ty), !v.m_path.m_bindings.type.binding.is_EnumVar(), true, mv$(values)));
    }

    virtual void visit(::AST::ExprNode_Array& v) override {
        if (v.m_size) {
            m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_ArraySized>(
                v.span(),
                lower(v.m_values.at(0)),
                // TODO: Should this size be a full expression on its own?
                lower(v.m_size)
            ));
        } else {
            ::std::vector<::HIR::ExprNodeP> vals;
            for (auto& val : v.m_values) {
                vals.push_back(lower(val));
            }
            m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_ArrayList>(v.span(), mv$(vals)));
        }
    }

    virtual void visit(::AST::ExprNode_Tuple& v) override {
        ::std::vector<::HIR::ExprNodeP> vals;
        for (auto& val : v.m_values) {
            vals.push_back(lower(val));
        }
        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Tuple>(v.span(), mv$(vals)));
    }

    virtual void visit(::AST::ExprNode_NamedValue& v) override {
        if (const auto* e = v.m_path.m_class.opt_Local()) {
            TU_MATCH_HDRA( (v.m_path.m_bindings.value.binding), {)
            default:
                BUG(v.span(), "Named value was a local, but wasn't bound to a known type - " << v.m_path);
                TU_ARMA(Generic, binding) {
                    m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_ConstParam>(v.span(), e->name, binding.index));
                }
                TU_ARMA(Variable, binding) {
                    m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Variable>(v.span(), e->name, binding.slot));
                }
            }
        } else {
            TU_MATCH_HDRA( (v.m_path.m_bindings.value.binding), {)
            TU_ARMA(Struct, e) {
                    ASSERT_BUG(v.span(), e.struct_ || e.hir, "PathValue bound to a struct but pointer not set - " << v.m_path);
                    // Check the form and emit a PathValue if not a unit
                    bool is_tuple_constructor = false;
                    if (e.struct_) {
                        if (e.struct_->m_data.is_Struct()) {
                            ERROR(v.span(), E0000, "Named value referring to a struct that isn't tuple-like or unit-like - " << v.m_path);
                        }
                        is_tuple_constructor = e.struct_->m_data.is_Tuple();
                    } else {
                        const auto& str = *e.hir;
                        if (str.m_data.is_Unit()) {
                            is_tuple_constructor = false;
                        } else if (str.m_data.is_Tuple()) {
                            is_tuple_constructor = true;
                        } else {
                            ERROR(v.span(), E0000, "Named value referring to a struct that isn't tuple-like or unit-like - " << v.m_path);
                        }
                    }
                    if (is_tuple_constructor) {
                        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_PathValue>(v.span(), LowerHIR_Path(v.span(), v.m_path, FromAST_PathClass::Value), ::HIR::ExprNode_PathValue::STRUCT_CONSTR));
                    } else {
                        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_UnitVariant>(v.span(), LowerHIR_GenericPath(v.span(), v.m_path, FromAST_PathClass::Value), true));
                    }
                }
                TU_ARMA(EnumVar, e) {
                    ASSERT_BUG(v.span(), e.enum_ || e.hir, "PathValue bound to an enum but pointer not set - " << v.m_path);
                    const auto& var_name = v.m_path.nodes().back().name();
                    bool is_tuple_constructor = false;
                    unsigned int var_idx;
                    if (e.enum_) {
                        const auto& enm = *e.enum_;
                        auto it = ::std::find_if(enm.variants().begin(), enm.variants().end(), [&](const auto& x) {
                            return x.m_name == var_name;
                        });
                        assert(it != enm.variants().end());

                        var_idx = static_cast<unsigned int>(it - enm.variants().begin());
                        if (it->m_data.is_Struct()) {
                            ERROR(v.span(), E0000, "Named value referring to an enum that isn't tuple-like or unit-like - " << v.m_path);
                        }
                        is_tuple_constructor = it->m_data.is_Tuple() && it->m_data.as_Tuple().m_items.size() > 0;
                    } else {
                        const auto& enm = *e.hir;
                        auto idx = enm.find_variant(var_name);
                        assert(idx != SIZE_MAX);

                        var_idx = idx;
                        if (const auto* ee = enm.m_data.opt_Data()) {
                            if (ee->at(idx).type == g_crate_ptr->m_types.unit()) {
                            }
                            // TODO: Assert that it's not a struct-like
                            else {
                                is_tuple_constructor = true;
                            }
                        }
                    }
                    (void)var_idx; // TODO: Save time later by saving this.
                    if (is_tuple_constructor) {
                        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_PathValue>(v.span(), LowerHIR_Path(v.span(), v.m_path, FromAST_PathClass::Value), ::HIR::ExprNode_PathValue::ENUM_VAR_CONSTR));
                    } else {
                        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_UnitVariant>(v.span(), LowerHIR_GenericPath(v.span(), v.m_path, FromAST_PathClass::Value), false));
                    }
                }
                TU_ARMA(Function, e) {
                    m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_PathValue>(v.span(), LowerHIR_Path(v.span(), v.m_path, FromAST_PathClass::Value), ::HIR::ExprNode_PathValue::FUNCTION));
                }
                TU_ARMA(Static, e) {
                    if (e.static_) {
                        if (e.static_->s_class() != ::AST::Static::CONST) {
                            m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_PathValue>(v.span(), LowerHIR_Path(v.span(), v.m_path, FromAST_PathClass::Value), ::HIR::ExprNode_PathValue::STATIC));
                        } else {
                            m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_PathValue>(v.span(), LowerHIR_Path(v.span(), v.m_path, FromAST_PathClass::Value), ::HIR::ExprNode_PathValue::CONSTANT));
                        }
                    } else if (e.hir) {
                        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_PathValue>(v.span(), LowerHIR_Path(v.span(), v.m_path, FromAST_PathClass::Value), ::HIR::ExprNode_PathValue::STATIC));
                    }
                    // HACK: If the HIR pointer is nullptr, then it refers to a `const
                    else {
                        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_PathValue>(v.span(), LowerHIR_Path(v.span(), v.m_path, FromAST_PathClass::Value), ::HIR::ExprNode_PathValue::CONSTANT));
                    }
                }
                break;
                default:
                    auto p = LowerHIR_Path(v.span(), v.m_path, FromAST_PathClass::Value);
                    ASSERT_BUG(v.span(), !p.m_data.is_Generic(), "Unknown binding for PathValue but path is generic - " << v.m_path);
                    m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_PathValue>(v.span(), mv$(p), ::HIR::ExprNode_PathValue::UNKNOWN));
            }
        }
    }

    virtual void visit(::AST::ExprNode_Field& v) override {
        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Field>(v.span(), lower(v.m_obj), v.m_name));
    }

    virtual void visit(::AST::ExprNode_Index& v) override {
        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Index>(v.span(), lower(v.m_obj), lower(v.m_idx)));
    }

    virtual void visit(::AST::ExprNode_Deref& v) override {
        m_rv.reset(g_crate_ptr->m_pool->make<::HIR::ExprNode_Deref>(v.span(), lower(v.m_value)));
    }
};

::HIR::ExprPtr LowerHIR_ExprNode(const ::AST::ExprNode& e) {
    LowerHIR_ExprNode_Visitor v;

    const_cast<::AST::ExprNode*>(&e)->visit(v);

    if (!v.m_rv) {
        BUG(e.span(), typeid(e).name() << " - Yielded a nullptr HIR node");
    }

    struct InitialiseResultTypes final: ::HIR::ExprVisitorDef {
        explicit InitialiseResultTypes(::HIR::TypeInterner& types)
            : ::HIR::ExprVisitorDef(types)
        {
        }

        void visit_node_ptr(::HIR::ExprNodeP& node) override {
            node->m_res_type = type_interner().infer();
            node->visit(*this);
        }

        void visit_type(::HIR::TypeRef&) override {
        }
    } initialise(g_crate_ptr->m_types);
    initialise.visit_node_ptr(v.m_rv);

    return ::HIR::ExprPtr(mv$(v.m_rv));
}
