#include "lint_dead_code.h"

#include "span.h"
#include "hir_hir.h"
#include "hir_expr.h"
#include "lint_level.h"
#include "wire_board.h"
#include "hir_visitor.h"

#include <std/mem/obj_pool.h>
#include <std/sym/i_map.h>

using namespace stl;

namespace {
    const char* const LINT_NAME = "dead_code";

    struct Uses {
        IntMap<bool> names;

        explicit Uses(ObjPool* pool);

        void markName(const RcString& name);

        void mark(const HIRPath& path);
    };

    struct UseVisitor: public HIRExprVisitorDef {
        Uses& uses_;

        UseVisitor(HIRTypeInterner& types, Uses& uses);

        void visit(HIRExprNodeCallPath& node) override;

        void visit(HIRExprNodePathValue& node) override;

        void visit(HIRExprNodeAsm2& node) override;

        void visitPattern(const Span& sp, HIRPattern& pat) override;

        [[nodiscard]] const HIRType* visitType(const HIRType* ty) override;

        void visitTraitPath(HIRTraitPath& p) override;

        void visitPathParams(HIRPathParams& p) override;
    };

    struct UseCollector: public HIRVisitor {
        Uses& uses_;

        UseCollector(const WireBoard& wb, Uses& uses);

        void visitModule(HIRItemPath p, HIRModule& module) override;

        void visitExpr(HIRExprPtr& exp) override;

        void visitParams(HIRGenericParams& params) override;

        void visitGenericBound(HIRGenericBound& bound) override;

        void visitPattern(HIRPattern& pat) override;

        [[nodiscard]] const HIRType* visitType(const HIRType* ty) override;

        void visitTraitPath(HIRTraitPath& p) override;

        void visitPathParams(HIRPathParams& p) override;
    };

    struct Candidate {
        const HIRFunction* function;
        RcString name;
        CfgLintLevel level;
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

    bool isLangItem(const HIRCrate& crate, const HIRSimplePath& path) {
        for (const auto& item : crate.langItems) {
            if (item.second == path) {
                return true;
            }
        }
        return false;
    }

    void collectCandidates(const Settings& settings, const HIRCrate& crate, const HIRSimplePath& modulePath, const HIRModule& module, CfgLintLevel inherited, Vector<Candidate>& out) {
        const auto level = ApplyLintLevelOverrides(settings, module.lintLevels, LINT_NAME, inherited);
        const bool isRoot = modulePath.components().empty();
        auto parentPath = modulePath;
        if (!isRoot) {
            parentPath.popComponent();
        }
        for (const auto& named : module.valueItems) {
            const auto& entry = *named.second;
            if (!entry.ent.is_Function()) {
                continue;
            }
            const auto& function = *entry.ent.as_Function();
            const auto& name = named.first;
            if (entry.publicity.isGlobal() || (!isRoot && entry.publicity.isVisible(parentPath))) {
                continue;
            }
            if (name.c_str()[0] == '_' || (isRoot && name == "main") || !function.code || function.isConst || function.linkage.name != "") {
                continue;
            }
            if (spanIsNotUserCode(function.span, crate.crateName) || isLangItem(crate, modulePath + name)) {
                continue;
            }
            const auto functionLevel = ApplyLintLevelOverrides(settings, function.markings.lintLevels, LINT_NAME, level);
            if (functionLevel != CfgLintLevel::Allow) {
                out.pushBack(Candidate{&function, name, functionLevel});
            }
        }
        for (const auto& named : module.modItems) {
            if (const auto* child = named.second->ent.opt_Module()) {
                collectCandidates(settings, crate, modulePath + named.first, *child, level, out);
            }
        }
    }
}

void LintDeadCode(const WireBoard& wb, HIRCrate& crate) {
    const auto& settings = *wb.settings;
    Vector<Candidate> candidates;
    collectCandidates(settings, crate, HIRSimplePath(crate.crateName), crate.rootModule, settings.lintLevel(RcString::newInterned(LINT_NAME), CfgLintLevel::Warn), candidates);
    if (candidates.length() == 0) {
        return;
    }

    auto poolOwner = ObjPool::fromMemory();
    Uses uses(poolOwner.mutPtr());
    for (size_t i = 0; i < candidates.length(); i++) {
        uses.names.insert(candidates[i].name.rawId(), false);
    }
    UseCollector collector(wb, uses);
    collector.visitCrate(crate);

    for (size_t i = 0; i < candidates.length(); i++) {
        const auto& candidate = candidates[i];
        if (*uses.names.find(candidate.name.rawId())) {
            continue;
        }
        switch (candidate.level) {
            case CfgLintLevel::Allow:
                break;
            case CfgLintLevel::Warn:
            case CfgLintLevel::ForceWarn:
                WARNING(candidate.function->span, W0000, StringView("function `") << candidate.name << StringView("` is never used"));
                break;
            case CfgLintLevel::Deny:
            case CfgLintLevel::Forbid:
                ERROR(candidate.function->span, E0000, StringView("function `") << candidate.name << StringView("` is never used"));
                break;
        }
    }
}

Uses::Uses(ObjPool* pool)
    : names(pool)
{
}

auto Uses::markName(const RcString& name) -> void {
    if (auto* used = names.find(name.rawId())) {
        *used = true;
    }
}

auto Uses::mark(const HIRPath& path) -> void {
    if (const auto* generic = path.data.opt_Generic(); generic && !generic->path.components().empty()) {
        markName(generic->path.components().back());
    }
}

UseVisitor::UseVisitor(HIRTypeInterner& types, Uses& uses)
    : HIRExprVisitorDef(types)
    , uses_(uses)
{
}

auto UseVisitor::visit(HIRExprNodeCallPath& node) -> void {
    uses_.mark(node.path);
    HIRExprVisitorDef::visit(node);
}

auto UseVisitor::visit(HIRExprNodePathValue& node) -> void {
    if (node.target == HIRExprNodePathValue::FUNCTION) {
        uses_.mark(node.path);
    }
    HIRExprVisitorDef::visit(node);
}

auto UseVisitor::visit(HIRExprNodeAsm2& node) -> void {
    for (const auto& param : node.params) {
        if (const auto* sym = param.opt_Sym()) {
            uses_.mark(*sym);
        }
    }
    HIRExprVisitorDef::visit(node);
}

auto UseVisitor::visitPattern(const Span&, HIRPattern&) -> void {
}

[[nodiscard]] auto UseVisitor::visitType(const HIRType* ty) -> const HIRType* {
    return ty;
}

auto UseVisitor::visitTraitPath(HIRTraitPath&) -> void {
}

auto UseVisitor::visitPathParams(HIRPathParams&) -> void {
}

UseCollector::UseCollector(const WireBoard& wb, Uses& uses)
    : HIRVisitor(nullptr, wb.crate->types)
    , uses_(uses)
{
}

auto UseCollector::visitModule(HIRItemPath p, HIRModule& module) -> void {
    for (const auto& named : module.valueItems) {
        if (const auto* import = named.second->ent.opt_Import(); import && !import->path.components().empty()) {
            uses_.markName(import->path.components().back());
        }
    }
    for (const auto& item : module.globalAsm) {
        for (const auto& operand : item.operands) {
            if (const auto* sym = operand.opt_Sym()) {
                uses_.mark(*sym);
            }
        }
    }
    HIRVisitor::visitModule(p, module);
}

auto UseCollector::visitExpr(HIRExprPtr& exp) -> void {
    if (exp) {
        UseVisitor visitor(this->typeInterner(), uses_);
        exp->visit(visitor);
    }
}

auto UseCollector::visitParams(HIRGenericParams&) -> void {
}

auto UseCollector::visitGenericBound(HIRGenericBound&) -> void {
}

auto UseCollector::visitPattern(HIRPattern&) -> void {
}

[[nodiscard]] auto UseCollector::visitType(const HIRType* ty) -> const HIRType* {
    return ty;
}

auto UseCollector::visitTraitPath(HIRTraitPath&) -> void {
}

auto UseCollector::visitPathParams(HIRPathParams&) -> void {
}
