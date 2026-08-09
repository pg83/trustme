/*
 * MRustC - Rust Compiler
 * - By John Hodge (Mutabah/thePowersGang)
 *
 * hir_typeck/common.hpp
 * - Typecheck common methods
 */
#pragma once

#include "hir_typeck_impl_ref.hpp"
#include "hir_generic_params.hpp"
#include "hir_type.hpp"
#include "hir_typeck_monomorph.hpp"

typedef ::std::function<bool(const ::HIR::TypeRef&)> t_cb_visit_ty;
/// Calls the provided callback on every type seen when recursing the type.
/// If the callback returns `true`, no further types are visited and the function returns `true`.
extern bool visit_ty_with(const ::HIR::TypeRef&, t_cb_visit_ty callback);
extern bool visit_trait_path_tys_with(const ::HIR::TraitPath&, t_cb_visit_ty callback);
extern bool visit_path_tys_with(const ::HIR::Path&, t_cb_visit_ty callback);

typedef ::std::function<bool(::HIR::TypeRef& rewritten, ::HIR::TypeData& data)> t_cb_rewrite_ty;
extern bool rewrite_ty_with(::HIR::TypeInterner& types, ::HIR::TypeRef& ty, t_cb_rewrite_ty callback);
extern bool rewrite_path_tys_with(::HIR::TypeInterner& types, ::HIR::Path& path, t_cb_rewrite_ty callback);

typedef ::std::function<bool(const ::HIR::TypeRef&, ::HIR::TypeRef&)> t_cb_clone_ty;
/// Clones a type, calling the provided callback on every type (optionally providing a replacement)
///
/// Closure should return `true` if the passed output slot was populated.
extern ::HIR::TypeRef clone_ty_with(::HIR::TypeInterner& types, const Span& sp, const ::HIR::TypeRef& tpl, t_cb_clone_ty callback);
extern ::HIR::PathParams clone_path_params_with(::HIR::TypeInterner& types, const Span& sp, const ::HIR::PathParams& tpl, t_cb_clone_ty callback);

extern void check_type_class_primitive(const Span& sp, const ::HIR::TypeRef& type, ::HIR::InferClass ic, ::HIR::CoreType ct);

namespace typeck {
    // The primitive operation is a language candidate, separate from an
    // implementation of the operator trait.  Keeping this classification in
    // one place makes type checking, UFCS expansion, and validation agree on
    // which expressions may remain as MIR primitive operations.
    enum class PrimitiveOperator {
        None,

        Add,
        Sub,
        Mul,
        Div,
        Rem,
        BitAnd,
        BitOr,
        BitXor,
        Shl,
        Shr,
        Equal,
        Order,
        Not,
        Neg,
        Deref,

        AddAssign,
        SubAssign,
        MulAssign,
        DivAssign,
        RemAssign,
        BitAndAssign,
        BitOrAssign,
        BitXorAssign,
        ShlAssign,
        ShrAssign,
    };

    inline bool primitive_operator_has_builtin(PrimitiveOperator op, const ::HIR::TypeRef& left, const ::HIR::TypeRef& right) {
        const auto* left_primitive = left->opt_Primitive();
        const auto* right_primitive = right->opt_Primitive();

        const auto same_numeric = [&]() {
            return left == right && left_primitive && (::HIR::is_integer(*left_primitive) || ::HIR::is_float(*left_primitive));
        };
        const auto same_bitwise = [&]() {
            return left == right && left_primitive && (::HIR::is_integer(*left_primitive) || *left_primitive == ::HIR::CoreType::Bool);
        };
        const auto shift = [&]() {
            return left_primitive && right_primitive && ::HIR::is_integer(*left_primitive) && ::HIR::is_integer(*right_primitive);
        };
        const auto comparison = [&]() {
            if (left != right) {
                return false;
            }
            return left->is_Pointer() || (left_primitive && *left_primitive != ::HIR::CoreType::Str);
        };

        switch (op) {
            case PrimitiveOperator::Add:
            case PrimitiveOperator::Sub:
            case PrimitiveOperator::Mul:
            case PrimitiveOperator::Div:
            case PrimitiveOperator::Rem:
            case PrimitiveOperator::AddAssign:
            case PrimitiveOperator::SubAssign:
            case PrimitiveOperator::MulAssign:
            case PrimitiveOperator::DivAssign:
            case PrimitiveOperator::RemAssign:
                return same_numeric();

            case PrimitiveOperator::BitAnd:
            case PrimitiveOperator::BitOr:
            case PrimitiveOperator::BitXor:
            case PrimitiveOperator::BitAndAssign:
            case PrimitiveOperator::BitOrAssign:
            case PrimitiveOperator::BitXorAssign:
                return same_bitwise();

            case PrimitiveOperator::Shl:
            case PrimitiveOperator::Shr:
            case PrimitiveOperator::ShlAssign:
            case PrimitiveOperator::ShrAssign:
                return shift();

            case PrimitiveOperator::Equal:
            case PrimitiveOperator::Order:
                return comparison();

            case PrimitiveOperator::None:
            case PrimitiveOperator::Not:
            case PrimitiveOperator::Neg:
            case PrimitiveOperator::Deref:
                return false;
        }
        throw "";
    }

    // For these binary language operations, once the left-hand type is known
    // it also fixes an otherwise untyped right-hand operand. Shifts are
    // deliberately excluded: their right-hand side need only be an integer
    // and may have a different type.
    inline bool primitive_operator_lhs_determines_rhs(PrimitiveOperator op, const ::HIR::TypeRef& left) {
        const auto* primitive = left->opt_Primitive();
        const auto numeric = primitive && (::HIR::is_integer(*primitive) || ::HIR::is_float(*primitive));
        const auto bitwise = primitive && (::HIR::is_integer(*primitive) || *primitive == ::HIR::CoreType::Bool);
        const auto comparison = left->is_Pointer() || (primitive && *primitive != ::HIR::CoreType::Str);

        switch (op) {
            case PrimitiveOperator::Add:
            case PrimitiveOperator::Sub:
            case PrimitiveOperator::Mul:
            case PrimitiveOperator::Div:
            case PrimitiveOperator::Rem:
            case PrimitiveOperator::AddAssign:
            case PrimitiveOperator::SubAssign:
            case PrimitiveOperator::MulAssign:
            case PrimitiveOperator::DivAssign:
            case PrimitiveOperator::RemAssign:
                return numeric;

            case PrimitiveOperator::BitAnd:
            case PrimitiveOperator::BitOr:
            case PrimitiveOperator::BitXor:
            case PrimitiveOperator::BitAndAssign:
            case PrimitiveOperator::BitOrAssign:
            case PrimitiveOperator::BitXorAssign:
                return bitwise;

            case PrimitiveOperator::Equal:
            case PrimitiveOperator::Order:
                return comparison;

            case PrimitiveOperator::Shl:
            case PrimitiveOperator::Shr:
            case PrimitiveOperator::ShlAssign:
            case PrimitiveOperator::ShrAssign:
            case PrimitiveOperator::None:
            case PrimitiveOperator::Not:
            case PrimitiveOperator::Neg:
            case PrimitiveOperator::Deref:
                return false;
        }
        throw "";
    }

    // A binary language candidate is available either when both operands are
    // already known to be valid primitive inputs, or when the known lhs
    // determines the still-inferred rhs.
    inline bool primitive_operator_has_language_candidate(PrimitiveOperator op, const ::HIR::TypeRef& left, const ::HIR::TypeRef& right) {
        return primitive_operator_has_builtin(op, left, right)
            || (right->is_Infer() && primitive_operator_lhs_determines_rhs(op, left));
    }

    inline bool primitive_operator_has_builtin(PrimitiveOperator op, const ::HIR::TypeRef& value) {
        if (op == PrimitiveOperator::Deref) {
            return value->is_Borrow() || value->is_Pointer();
        }

        const auto* primitive = value->opt_Primitive();
        if (!primitive) {
            return false;
        }

        switch (op) {
            case PrimitiveOperator::Not:
                return *primitive == ::HIR::CoreType::Bool || ::HIR::is_integer(*primitive);
            case PrimitiveOperator::Neg:
                if (::HIR::is_float(*primitive)) {
                    return true;
                }
                switch (*primitive) {
                    case ::HIR::CoreType::Isize:
                    case ::HIR::CoreType::I8:
                    case ::HIR::CoreType::I16:
                    case ::HIR::CoreType::I32:
                    case ::HIR::CoreType::I64:
                    case ::HIR::CoreType::I128:
                        return true;
                    default:
                        return false;
                }
            case PrimitiveOperator::Deref:
                return false;
            default:
                return false;
        }
    }
}

class StaticTraitResolve;
extern void Typecheck_Expressions_ValidateOne(const StaticTraitResolve& resolve, const ::std::vector<::std::pair<::HIR::Pattern, ::HIR::TypeRef>>& args, const ::HIR::TypeRef& ret_ty, const ::HIR::ExprPtr& code);
