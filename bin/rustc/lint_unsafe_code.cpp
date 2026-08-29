#include "lint_unsafe_code.h"

#include "span.h"
#include "hir_hir.h"
#include "hir_expr.h"
#include "lint_level.h"
#include "wire_board.h"
#include "hir_visitor.h"

using namespace stl;

namespace {
    const char* const LINT_NAME = "unsafe_code";

    struct UnsafeBlockVisitor: public HIRExprVisitorDef {
        CfgLintLevel level_;
        const RcString& crateName_;

        UnsafeBlockVisitor(HIRTypeInterner& types, CfgLintLevel level, const RcString& crateName);

        void visit(HIRExprNodeBlock& node) override;
    };

    struct UnsafeCodeVisitor: public HIRVisitor {
        const HIRCrate& crate_;
        const Settings& settings_;
        CfgLintLevel level_;
        RcString crateName_;

        UnsafeCodeVisitor(const WireBoard& wb);

        void visitModule(HIRItemPath p, HIRModule& module) override;

        void visitTypeImpl(HIRTypeImpl& impl) override;

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override;

        void visitFunction(HIRItemPath p, HIRFunction& item) override;

        void visitExpr(HIRExprPtr& exp) override;
    };

    bool spanIsNotUserCode(const Span& sp, const RcString& crateName) {
        for (Span frame = sp; frame; frame = frame->parentSpan) {
            if (const auto* macro = cast<const SpanInnerMacro>(frame.get())) {
                if (macro->crate != crateName) {
                    return true;
                }
            }
        }
        return false;
    }

    void report(CfgLintLevel level, const Span& sp, const char* what) {
        switch (level) {
            case CfgLintLevel::Allow:
                break;
            case CfgLintLevel::Warn:
            case CfgLintLevel::ForceWarn:
                WARNING(sp, W0000, StringView("usage of ") << what);
                break;
            case CfgLintLevel::Deny:
            case CfgLintLevel::Forbid:
                ERROR(sp, E0000, StringView("usage of ") << what);
                break;
        }
    }
}

void LintUnsafeCode(const WireBoard& wb, HIRCrate& crate) {
    UnsafeCodeVisitor visitor(wb);
    visitor.visitCrate(crate);
}

UnsafeBlockVisitor::UnsafeBlockVisitor(HIRTypeInterner& types, CfgLintLevel level, const RcString& crateName)
    : HIRExprVisitorDef(types)
    , level_(level)
    , crateName_(crateName)
{
}

auto UnsafeBlockVisitor::visit(HIRExprNodeBlock& node) -> void {
    if (node.isUnsafe && !spanIsNotUserCode(node.span(), crateName_)) {
        report(level_, node.span(), "an `unsafe` block");
    }
    HIRExprVisitorDef::visit(node);
}

UnsafeCodeVisitor::UnsafeCodeVisitor(const WireBoard& wb)
    : HIRVisitor(nullptr, wb.crate->types)
    , crate_(*wb.crate)
    , settings_(*wb.settings)
    , level_(wb.settings->lintLevel(LINT_NAME, CfgLintLevel::Allow))
    , crateName_(wb.crate->crateName)
{
}

auto UnsafeCodeVisitor::visitModule(HIRItemPath p, HIRModule& module) -> void {
    const auto saved = level_;
    level_ = ApplyLintLevelOverrides(settings_, module.lintLevels, LINT_NAME, level_);
    HIRVisitor::visitModule(p, module);
    level_ = saved;
}

auto UnsafeCodeVisitor::visitTypeImpl(HIRTypeImpl& impl) -> void {
    const auto saved = level_;
    level_ = LintLevelForModulePath(settings_, crate_, impl.srcModule, LINT_NAME, CfgLintLevel::Allow);
    HIRVisitor::visitTypeImpl(impl);
    level_ = saved;
}

auto UnsafeCodeVisitor::visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) -> void {
    const auto saved = level_;
    level_ = LintLevelForModulePath(settings_, crate_, impl.srcModule, LINT_NAME, CfgLintLevel::Allow);
    HIRVisitor::visitTraitImpl(traitPath, impl);
    level_ = saved;
}

auto UnsafeCodeVisitor::visitFunction(HIRItemPath p, HIRFunction& item) -> void {
    const auto saved = level_;
    level_ = ApplyLintLevelOverrides(settings_, item.markings.lintLevels, LINT_NAME, level_);
    if (item.unsafe && item.code && !spanIsNotUserCode(item.code->span(), crateName_)) {
        report(level_, item.code->span(), "an `unsafe` function");
    }
    HIRVisitor::visitFunction(p, item);
    level_ = saved;
}

auto UnsafeCodeVisitor::visitExpr(HIRExprPtr& exp) -> void {
    if (exp && level_ != CfgLintLevel::Allow) {
        UnsafeBlockVisitor visitor(this->typeInterner(), level_, crateName_);
        exp->visit(visitor);
    }
}
