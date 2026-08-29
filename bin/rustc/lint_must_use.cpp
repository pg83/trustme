#include "lint_must_use.h"

#include "span.h"
#include "hir_hir.h"
#include "hir_expr.h"
#include "lint_level.h"
#include "wire_board.h"
#include "hir_visitor.h"

using namespace stl;

namespace {
    const char* const LINT_NAME = "unused_must_use";

    struct MustUseVisitor: public HIRExprVisitorDef {
        const HIRCrate& crate_;
        CfgLintLevel level_;

        MustUseVisitor(const HIRCrate& crate, CfgLintLevel level);

        void visit(HIRExprNodeBlock& node) override;

        void checkDiscarded(const HIRExprNode& statement);

        void report(const Span& sp, const char* what);
    };

    struct MustUseOuterVisitor: public HIRVisitor {
        const HIRCrate& crate_;
        const Settings& settings_;
        CfgLintLevel level_;

        MustUseOuterVisitor(const WireBoard& wb, CfgLintLevel level);

        void visitModule(HIRItemPath p, HIRModule& module) override;

        void visitTypeImpl(HIRTypeImpl& impl) override;

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override;

        void visitFunction(HIRItemPath p, HIRFunction& item) override;

        void visitExpr(HIRExprPtr& exp) override;
    };

    const HIRExprNode* peelBlocks(const HIRExprNode* node) {
        while (const auto* block = cast<const HIRExprNodeBlock>(node)) {
            if (!block->valueNode || !block->nodes.empty()) {
                break;
            }
            node = &*block->valueNode;
        }
        return node;
    }

    bool typeIsMustUse(const HIRTypeData* ty) {
        if (const auto* pe = ty->opt_Path()) {
            switch (pe->binding.tag()) {
                default:
                    return false;
                case HIRTypePathBinding::TAG_Struct: {
                    auto& be = pe->binding.as_Struct();
                    return be->mustUse;
                }
                case HIRTypePathBinding::TAG_Enum: {
                    auto& be = pe->binding.as_Enum();
                    return be->mustUse;
                }
                case HIRTypePathBinding::TAG_Union: {
                    auto& be = pe->binding.as_Union();
                    return be->mustUse;
                }
            }
        }
        return false;
    }

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
}

CfgLintLevel LintUnusedMustUseLevel(const Settings& settings) {
    return settings.lintLevel(LINT_NAME, CfgLintLevel::Warn);
}

void LintUnusedMustUse(const WireBoard& wb, HIRCrate& crate) {
    const auto level = LintUnusedMustUseLevel(*wb.settings);
    MustUseOuterVisitor visitor(wb, level);
    visitor.visitCrate(crate);
}

MustUseVisitor::MustUseVisitor(const HIRCrate& crate, CfgLintLevel level)
    : HIRExprVisitorDef(crate.types)
    , crate_(crate)
    , level_(level)
{
}

auto MustUseVisitor::visit(HIRExprNodeBlock& node) -> void {
    for (const auto& statement : node.nodes) {
        if (statement) {
            this->checkDiscarded(*statement);
        }
    }
    HIRExprVisitorDef::visit(node);
}

auto MustUseVisitor::checkDiscarded(const HIRExprNode& statement) -> void {
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

auto MustUseVisitor::report(const Span& sp, const char* what) -> void {
    switch (level_) {
        case CfgLintLevel::Allow:
            break;
        case CfgLintLevel::Warn:
        case CfgLintLevel::ForceWarn:
            WARNING(sp, W0000, StringView("unused ") << what << StringView(" that must be used"));
            break;
        case CfgLintLevel::Deny:
        case CfgLintLevel::Forbid:
            ERROR(sp, E0000, StringView("unused ") << what << StringView(" that must be used"));
            break;
    }
}

MustUseOuterVisitor::MustUseOuterVisitor(const WireBoard& wb, CfgLintLevel level)
    : HIRVisitor(nullptr, wb.crate->types)
    , crate_(*wb.crate)
    , settings_(*wb.settings)
    , level_(level)
{
}

auto MustUseOuterVisitor::visitModule(HIRItemPath p, HIRModule& module) -> void {
    const auto saved = level_;
    level_ = ApplyLintLevelOverrides(settings_, module.lintLevels, LINT_NAME, level_);
    HIRVisitor::visitModule(p, module);
    level_ = saved;
}

auto MustUseOuterVisitor::visitTypeImpl(HIRTypeImpl& impl) -> void {
    const auto saved = level_;
    level_ = LintLevelForModulePath(settings_, crate_, impl.srcModule, LINT_NAME, CfgLintLevel::Warn);
    HIRVisitor::visitTypeImpl(impl);
    level_ = saved;
}

auto MustUseOuterVisitor::visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) -> void {
    const auto saved = level_;
    level_ = LintLevelForModulePath(settings_, crate_, impl.srcModule, LINT_NAME, CfgLintLevel::Warn);
    HIRVisitor::visitTraitImpl(traitPath, impl);
    level_ = saved;
}

auto MustUseOuterVisitor::visitFunction(HIRItemPath p, HIRFunction& item) -> void {
    const auto saved = level_;
    level_ = ApplyLintLevelOverrides(settings_, item.markings.lintLevels, LINT_NAME, level_);
    HIRVisitor::visitFunction(p, item);
    level_ = saved;
}

auto MustUseOuterVisitor::visitExpr(HIRExprPtr& exp) -> void {
    if (exp) {
        MustUseVisitor visitor(crate_, level_);
        exp->visit(visitor);
    }
}
