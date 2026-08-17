#include "lint_must_use.h"

#include "hir_expr.h"
#include "hir_hir.h"
#include "hir_visitor.h"
#include "lint_level.h"
#include "span.h"
#include "wire_board.h"

namespace {
    const char* const LINT_NAME = "unused_must_use";

    /// Peel the wrappers rustc's lint looks through: a block that only yields a
    /// value is the value, `unsafe` block included.
    const HIRExprNode* peelBlocks(const HIRExprNode* node) {
        while (const auto* block = cast<const HIRExprNodeBlock>(node)) {
            if (!block->valueNode || !block->nodes.empty()) {
                break;
            }
            node = &*block->valueNode;
        }
        return node;
    }

    /// `#[must_use]` on the type of the discarded value.
    bool typeIsMustUse(const HIRTypeData* ty) {
        if (const auto* pe = ty->opt_Path()) {
            TU_MATCH_HDRA( (pe->binding), {)
            default:
                return false;
                TU_ARMA(Struct, be) {
                    return be->mustUse;
                }
                TU_ARMA(Enum, be) {
                    return be->mustUse;
                }
                TU_ARMA(Union, be) {
                    return be->mustUse;
                }
            }
        }
        return false;
    }

    /// `#[must_use]` on a trait named by `impl Trait` or `dyn Trait`.
    bool traitIsMustUse(const HIRCrate& crate, const HIRTypeData* ty) {
        auto check = [&](const HIRSimplePath& path) {
            return crate.getTraitByPath(Span(), path).mustUse;
        };
        if (const auto* te = ty->opt_TraitObject()) {
            return check(te->trait.path.path);
        }
        if (const auto* te = ty->opt_ErasedType()) {
            for (const auto& trait : te->traits) {
                if (check(trait.path.path)) {
                    return true;
                }
            }
        }
        return false;
    }

    class MustUseVisitor: public HIRExprVisitorDef {
        const HIRCrate& crate_;
        CfgLintLevel level_;

    public:
        MustUseVisitor(const HIRCrate& crate, CfgLintLevel level)
            : HIRExprVisitorDef(crate.types)
            , crate_(crate)
            , level_(level)
        {
        }

        void visit(HIRExprNodeBlock& node) override {
            for (const auto& statement : node.nodes) {
                if (statement) {
                    this->checkDiscarded(*statement);
                }
            }
            HIRExprVisitorDef::visit(node);
        }

    private:
        void checkDiscarded(const HIRExprNode& statement) {
            const auto* node = peelBlocks(&statement);

            const HIRFunction* called = nullptr;
            if (const auto* call = cast<const HIRExprNodeCallPath>(node)) {
                called = call->cache.fcn;
            } else if (const auto* call = cast<const HIRExprNodeCallMethod>(node)) {
                called = call->cache.fcn;
            }
            if (called && called->markings.mustUse) {
                this->report(node->span(), "return value");
                return;
            }

            if (node->resType) {
                if (typeIsMustUse(node->resType)) {
                    this->report(node->span(), "value");
                } else if (traitIsMustUse(crate_, node->resType)) {
                    this->report(node->span(), "implementor");
                }
            }
        }

        void report(const Span& sp, const char* what) {
            switch (level_) {
                case CfgLintLevel::Allow:
                    break;
                case CfgLintLevel::Warn:
                case CfgLintLevel::ForceWarn:
                    WARNING(sp, W0000, "unused " << what << " that must be used");
                    break;
                case CfgLintLevel::Deny:
                case CfgLintLevel::Forbid:
                    ERROR(sp, E0000, "unused " << what << " that must be used");
                    break;
            }
        }
    };

    class MustUseOuterVisitor: public HIRVisitor {
        const HIRCrate& crate_;
        const Settings& settings_;
        CfgLintLevel level_;

    public:
        MustUseOuterVisitor(const WireBoard& wb, CfgLintLevel level)
            : HIRVisitor(nullptr, wb.crate->types)
            , crate_(*wb.crate)
            , settings_(*wb.settings)
            , level_(level)
        {
        }

        /// A lint attribute on the function overrides the crate's level for its
        /// body. An exact name beats a group whichever order they were written
        /// in, which is why the two are recorded apart.
        void visitFunction(HIRItemPath p, HIRFunction& item) override {
            const auto saved = level_;
            level_ = LintLevelForItem(settings_, item.markings.lintLevels, item.markings.lintGroupLevels, LINT_NAME, CfgLintLevel::Warn);
            HIRVisitor::visitFunction(p, item);
            level_ = saved;
        }

        void visitExpr(HIRExprPtr& exp) override {
            if (exp) {
                MustUseVisitor visitor(crate_, level_);
                exp->visit(visitor);
            }
        }
    };
}

CfgLintLevel LintUnusedMustUseLevel(const Settings& settings) {
    return settings.lintLevel(LINT_NAME, CfgLintLevel::Warn);
}

void LintUnusedMustUse(const WireBoard& wb, HIRCrate& crate) {
    const auto level = LintUnusedMustUseLevel(*wb.settings);
    // A function may still raise the level for its own body, so the walk runs
    // even when the crate allows the lint.
    (void)level;
    MustUseOuterVisitor visitor(wb, level);
    visitor.visitCrate(crate);
}
