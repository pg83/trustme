#include "lint_unsafe_code.h"

#include "hir_expr.h"
#include "hir_hir.h"
#include "hir_visitor.h"
#include "lint_level.h"
#include "span.h"
#include "wire_board.h"

namespace {
    const char* const LINT_NAME = "unsafe_code";

    /// Code that a macro from another crate expanded to is not the caller's to
    /// fix, and the standard library relies on that: `thread_local!` is unsafe
    /// inside while its users may forbid unsafe code.
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
                WARNING(sp, W0000, "usage of " << what);
                break;
            case CfgLintLevel::Deny:
            case CfgLintLevel::Forbid:
                ERROR(sp, E0000, "usage of " << what);
                break;
        }
    }

    class UnsafeBlockVisitor: public HIRExprVisitorDef {
        CfgLintLevel level_;
        const RcString& crateName_;

    public:
        UnsafeBlockVisitor(HIRTypeInterner& types, CfgLintLevel level, const RcString& crateName)
            : HIRExprVisitorDef(types)
            , level_(level)
            , crateName_(crateName)
        {
        }

        void visit(HIRExprNodeBlock& node) override {
            if (node.isUnsafe && !spanIsNotUserCode(node.span(), crateName_)) {
                report(level_, node.span(), "an `unsafe` block");
            }
            HIRExprVisitorDef::visit(node);
        }
    };

    class UnsafeCodeVisitor: public HIRVisitor {
        const Settings& settings_;
        CfgLintLevel level_;
        RcString crateName_;

    public:
        UnsafeCodeVisitor(const WireBoard& wb)
            : HIRVisitor(nullptr, wb.crate->types)
            , settings_(*wb.settings)
            , level_(wb.settings->lintLevel(LINT_NAME, CfgLintLevel::Allow))
            , crateName_(wb.crate->crateName)
        {
        }

        /// A lint attribute on the function sets the level for its declaration
        /// and its body.
        void visitFunction(HIRItemPath p, HIRFunction& item) override {
            const auto saved = level_;
            level_ = LintLevelForItem(settings_, item.markings.lintLevels, item.markings.lintGroupLevels, LINT_NAME, CfgLintLevel::Allow);
            if (item.unsafe && item.code && !spanIsNotUserCode(item.code->span(), crateName_)) {
                report(level_, item.code->span(), "an `unsafe` function");
            }
            HIRVisitor::visitFunction(p, item);
            level_ = saved;
        }

        void visitExpr(HIRExprPtr& exp) override {
            if (exp && level_ != CfgLintLevel::Allow) {
                UnsafeBlockVisitor visitor(this->typeInterner(), level_, crateName_);
                exp->visit(visitor);
            }
        }
    };
}

void LintUnsafeCode(const WireBoard& wb, HIRCrate& crate) {
    UnsafeCodeVisitor visitor(wb);
    visitor.visitCrate(crate);
}
