#include "resolve_common.h"

#include "output.h"
#include "ast_ast.h"
#include "hir_hir.h"
#include "ast_expr.h"
#include "settings.h"
#include "ast_crate.h"
#include "expand_cfg.h"
#include "synext_macro.h"
#include "resolve_main_bindings.h"

#include <std/alg/defer.h>

#include <span>

using namespace stl;

namespace {
    struct ResolveState {
        const Span& sp;
        const Settings& settings;
        const ASTCrate& crate;

        typedef std::pair<const ASTModule*, RcString> antirecurseStackEntT;
        std::vector<antirecurseStackEntT> antirecurseStack;

        ResolveState(const Span& span, const Settings& settings, const ASTCrate& crate);

        ResolveModuleRef getModule(const ASTPath& basePath, const ASTPath& path, bool ignoreLast, ASTAbsolutePath* outPath, bool ignoreHygiene = false);

        ResolveModuleRef getModuleForMacro(const ASTPath& basePath, const ASTPath& path, ASTAbsolutePath* outPath);

        ResolveModuleRef getModuleAst(const ASTModule& startMod, const ASTPath& path, size_t startOffset, bool ignoreLast, ASTAbsolutePath* outPath);

        ResolveModuleRef getModuleHir(const HIRModule& startMod, const ASTPath& path, size_t startOffset, bool ignoreLast, ASTAbsolutePath* outPath);

        const ASTModule& getModByTruePath(const std::vector<ASTPathNode>& baseNodes, size_t len);

        static bool matchingNamespace(const ASTItem& i, ResolveNamespace ns);

        ResolveItemRef findItem(const ASTModule& mod, const RcString& name, ResolveNamespace ns, ASTAbsolutePath* outPath = nullptr);

        ResolveItemRef findItemHir(const HIRModule& mod, const RcString& itemName, ResolveNamespace ns, ASTAbsolutePath* outPath = nullptr, const HIRSimplePath* visPathP = nullptr);
    };

    ASTAbsolutePath spToAp(const HIRSimplePath& sp) {
        return ASTAbsolutePath(sp.crateName(), sp.componentsVec());
    }

    ResolveItemRefType as_Namespace(ResolveItemRef ir) {
        if (ir.is_None()) {
            return ResolveItemRefType::make_None({});
        }
        return std::move(ir.as_Namespace());
    }
}

// TODO: Function that turns a relative path into a canonical absolute path to the containing module

ResolveModuleRef ResolveLookupGetModule(const Span& sp, const Settings& settings, const ASTCrate& crate, const ASTPath& basePath, ASTPath path, bool ignoreLast, ASTAbsolutePath* outPath) {
    ResolveState rs(sp, settings, crate);

    return rs.getModule(basePath, path, ignoreLast, outPath);
}

ResolveItemRefMacro ResolveLookupMacro(const Span& span, const Settings& settings, const ASTCrate& crate, const ASTPath& basePath, ASTPath path, ASTAbsolutePath* outPath) {
    TRACE_FUNCTION_F(StringView("path=") << path << StringView(" in ") << basePath);
    ResolveState rs(span, settings, crate);

    const auto& itemName = path.nodes().back().name();
    auto mod = rs.getModuleForMacro(basePath, path, outPath);
    if (mod.is_ImplicitPrelude()) {
        const auto& baseNodes = basePath.nodes();
        mod = ResolveModuleRef(&rs.getModByTruePath(baseNodes, baseNodes.size()));
    }
    switch (mod.tag()) {
        case ResolveModuleRef::TAG_Ast: {
            auto& modPtr = mod.as_Ast();
            auto rv = rs.findItem(*modPtr, itemName, ResolveNamespace::Macro, outPath);
            if (rv.is_None()) {
                return ResolveItemRefMacro::make_None({});
            }
            ASSERT_BUG(span, rv.is_Macro(), rv.tagStr());
            return std::move(rv.as_Macro());
        }
        case ResolveModuleRef::TAG_Hir: {
            auto& modPtr = mod.as_Hir();
            const HIRSimplePath* visPath = nullptr;
            HIRSimplePath tmpP;
            if (path.cls.is_Relative() && path.cls.as_Relative().hygiene.hasModPath()) {
                const auto& inP = path.cls.as_Relative().hygiene.modPath();
                tmpP = HIRSimplePath(inP.crate, inP.ents);
                DEBUG(StringView("vis_path=") << tmpP);
                visPath = &tmpP;
            }
            auto rv = rs.findItemHir(*modPtr, itemName, ResolveNamespace::Macro, outPath, visPath);
            if (rv.is_None()) {
                return ResolveItemRefMacro::make_None({});
            }
            ASSERT_BUG(span, rv.is_Macro(), rv.tagStr());
            return std::move(rv.as_Macro());
        }
        case ResolveModuleRef::TAG_ImplicitPrelude: {
            return ResolveItemRefMacro::make_None({});
        }
        case ResolveModuleRef::TAG_None: {
            return ResolveItemRefMacro::make_None({});
        }
    }
    // Technically a bug to reach this point.
    return ResolveItemRefMacro::make_None({});
}

ResolveModuleRef ResolveLookupGetModuleForName(const Span& sp, const Settings& settings, const ASTCrate& crate, const ASTPath& basePath, const ASTPath& path, ResolveNamespace ns, ASTAbsolutePath* outPath) {
    TRACE_FUNCTION_F(StringView("path=") << path << StringView(" in ") << basePath);
    ResolveState rs(sp, settings, crate);

    auto mod = rs.getModule(basePath, path, true, outPath);
    switch (mod.tag()) {
        case ResolveModuleRef::TAG_Ast: {
            auto& modPtr = mod.as_Ast();
            ASTAbsolutePath tmp;
            if (!outPath) {
                outPath = &tmp;
            }
            auto res = rs.findItem(*modPtr, path.nodes().back().name(), ns, outPath);
            if (res.is_None()) {
                BUG(sp, StringView("Unable to find ") << path << StringView(" (starting from ") << basePath << StringView(")"));
            }

            TODO(sp, StringView(""));
            break;
        }
        case ResolveModuleRef::TAG_Hir: {
            return mod;
        }
        case ResolveModuleRef::TAG_ImplicitPrelude: {
            return mod;
        }
        case ResolveModuleRef::TAG_None: {
            BUG(sp, StringView("Unable to find ") << path << StringView(" (starting from ") << basePath << StringView(")"));
            break;
        }
    }
    UNREACHABLE();
}

ResolveState::ResolveState(const Span& span, const Settings& settings, const ASTCrate& crate)
    : sp(span)
    , settings(settings)
    , crate(crate)
{
}

auto ResolveState::getModule(const ASTPath& basePath, const ASTPath& path, bool ignoreLast, ASTAbsolutePath* outPath, bool ignoreHygiene) -> ResolveModuleRef {
    TRACE_FUNCTION_F(path << StringView(" in ") << basePath << (ignoreLast ? " (ignore last)" : ""));
    const auto& baseNodes = basePath.nodes();
    switch (path.cls.tag()) {
        case ASTPathClass::TAG_Invalid: {
            BUG(sp, StringView("Invalid path class encountered"));
            break;
        }
        case ASTPathClass::TAG_Local: {
            BUG(sp, StringView("Local path class in use statement"));
            break;
        }
        case ASTPathClass::TAG_UFCS: {
            BUG(sp, StringView("UFCS path class in use statement"));
            break;
        }
        case ASTPathClass::TAG_Relative: {
            auto& e = path.cls.as_Relative();
            DEBUG(StringView("Relative ") << path);
            ASSERT_BUG(sp, !e.nodes.empty(), StringView(""));
            if (!ignoreHygiene && e.hygiene.hasModPath()) {
                const auto& mp = e.hygiene.modPath();
                if (mp.crate != "") {
                    ASSERT_BUG(sp, this->crate.externCrates.count(mp.crate), StringView("Crate not loaded for ") << mp);
                    const auto& crate = this->crate.externCrates.at(mp.crate);
                    const HIRModule* mod = &crate.hir->rootModule;
                    for (const auto& n : mp.ents) {
                        ASSERT_BUG(sp, mod->modItems.count(n), StringView("Node `") << n << StringView("` missing in path ") << mp);
                        const auto& i = *mod->modItems.at(n);
                        ASSERT_BUG(sp, i.ent.is_Module(), StringView("Node `") << n << StringView("` not a module in path ") << mp);
                        mod = &i.ent.as_Module();
                    }
                    if (outPath) {
                        outPath->crate = mp.crate;
                        outPath->nodes = mp.ents;
                    }
                    return getModuleHir(*mod, path, 1, ignoreLast, outPath);
                } else {
                    ASTPath p("", {});
                    for (const auto& n : mp.ents) {
                        p.nodes().push_back(n);
                    }
                    return getModule(p, path, ignoreLast, outPath, /*ignore_hygiene=*/true);
                }
            }
            if (e.nodes.size() == 1 && ignoreLast) {
                // HACK: If the target path is a crate name, then return `ImplicitPrelude` instead of the current module
                if (crate.edition >= ASTEdition::Rust2018) {
                    const auto& name = e.nodes.back().name();
                    DEBUG(StringView("Trying implicit externs for ") << name);
                    DEBUG(FMT_CB(os, for (const auto& v : settings.implicitCrates) { os << StringView(" ") << v.first; }));
                    auto ecIt = settings.implicitCrates.find(name);
                    if (ecIt != settings.implicitCrates.end()) {
                        return ResolveModuleRef::make_ImplicitPrelude({});
                    }
                }

                DEBUG(StringView("Ignore last"));
                const auto& currentMod = this->getModByTruePath(baseNodes, baseNodes.size());
                if (outPath) {
                    *outPath = currentMod.path();
                }
                return ResolveModuleRef(&currentMod);
            }
            const auto& name = e.nodes.front().name();
            size_t i = 0;
            do {
                const auto& startMod = this->getModByTruePath(baseNodes, baseNodes.size() - i);

                auto realMod = as_Namespace(this->findItem(startMod, name, ResolveNamespace::Namespace, outPath));
                switch (realMod.tag()) {
                    case ResolveItemRefType::TAG_Ast: {
                        auto& iData = realMod.as_Ast();
                        // TODO: What about an enum?
                        switch ((*iData).tag()) {
                            default: {
                            } break;
                            case ASTItem::TAG_Crate: {
                                auto& c = (*iData).as_Crate();
                                if (outPath) {
                                    *outPath = ASTAbsolutePath(c.name, {});
                                }
                                if (c.name == "") {
                                    return getModuleAst(crate.rootModule_, path, 1, ignoreLast, outPath);
                                }
                                ASSERT_BUG(sp, crate.externCrates.count(c.name) > 0, StringView("Unable to find crate `") << c.name << StringView("`"));
                                return getModuleHir(crate.externCrates.at(c.name).hir->rootModule, path, 1, ignoreLast, outPath);
                            }
                            case ASTItem::TAG_Module: {
                                auto& m = (*iData).as_Module();
                                return getModuleAst(m, path, 1, ignoreLast, outPath);
                            }
                        }
                        break;
                    }
                    case ResolveItemRefType::TAG_AstRoot: {
                        auto& m = realMod.as_AstRoot();
                        if (outPath) {
                            *outPath = ASTAbsolutePath("", {});
                        }
                        return getModuleAst(*m, path, 1, ignoreLast, outPath);
                    }
                    case ResolveItemRefType::TAG_HirRoot: {
                        auto& hirCrate = realMod.as_HirRoot();
                        return getModuleHir(hirCrate->rootModule, path, 1, ignoreLast, outPath);
                    }
                    case ResolveItemRefType::TAG_Hir: {
                        auto& iEntPtr = realMod.as_Hir();
                        ASSERT_BUG(sp, !iEntPtr->is_Import(), StringView(""));
                        if (iEntPtr->is_Enum()) {
                            DEBUG(StringView("Enum"));
                            return ResolveModuleRef();
                        }
                        if (iEntPtr->is_Trait()) {
                            return ResolveModuleRef();
                        }

                        ASSERT_BUG(sp, iEntPtr->is_Module(), StringView("Expected Module, got ") << iEntPtr->tagStr() << StringView(" for ") << name << StringView(" in [") << baseNodes << StringView("]"));
                        return getModuleHir(iEntPtr->as_Module(), path, 1, ignoreLast, outPath);
                        break;
                    }
                    case ResolveItemRefType::TAG_None: {
                        DEBUG(StringView("Keep searching (") << i << StringView("/") << baseNodes.size() << StringView(")"));
                        break;
                    }
                }

                i += 1;
            } while (i < baseNodes.size() && baseNodes[baseNodes.size() - i].name().c_str()[0] == '#');

            if (crate.edition >= ASTEdition::Rust2018 || name == "core" || name == "std") {
                DEBUG(StringView("Trying implicit externs for ") << name);
                DEBUG(FMT_CB(os, for (const auto& v : settings.implicitCrates) { os << StringView(" ") << v.first; }));
                auto ecIt = settings.implicitCrates.find(name);
                if (ecIt != settings.implicitCrates.end()) {
                    if (ecIt->second == "") {
                        return getModuleAst(crate.rootModule_, path, 1, ignoreLast, outPath);
                    } else {
                        ASSERT_BUG(sp, crate.externCrates.count(ecIt->second), StringView("Crate \"") << ecIt->second << StringView("\" not loaded (for \"") << ecIt->first << StringView("\")"));
                        const auto& ec = crate.externCrates.at(ecIt->second);
                        DEBUG(StringView("Implicitly imported crate"));
                        if (outPath) {
                            *outPath = ASTAbsolutePath(ecIt->second, {});
                        }
                        return getModuleHir(ec.hir->rootModule, path, 1, ignoreLast, outPath);
                    }
                }
            }
            DEBUG(StringView("Not found"));
            return ResolveModuleRef();
        }
        case ASTPathClass::TAG_Self: {
            DEBUG(StringView("Self ") << path);
            size_t i = 0;
            while (i < baseNodes.size() && baseNodes[baseNodes.size() - i - 1].name().c_str()[0] == '#') {
                i += 1;
            }
            const auto& startMod = this->getModByTruePath(baseNodes, baseNodes.size() - i);
            return getModuleAst(startMod, path, 0, ignoreLast, outPath);
        }
        case ASTPathClass::TAG_Super: {
            DEBUG(StringView("Super ") << path);
            size_t i = 0;
            while (i < baseNodes.size() && baseNodes[baseNodes.size() - i - 1].name().c_str()[0] == '#') {
                i += 1;
            }
            i += 1;
            ASSERT_BUG(sp, i <= baseNodes.size(), StringView(""));
            const auto& startMod = this->getModByTruePath(baseNodes, baseNodes.size() - i);
            return getModuleAst(startMod, path, 0, ignoreLast, outPath);
        }
        case ASTPathClass::TAG_Absolute: {
            auto& e = path.cls.as_Absolute();
            DEBUG(StringView("Absolute ") << path);
            if (e.crate == "" || e.crate == crate.crateNameReal) {
                DEBUG(StringView("Current crate"));
                return getModuleAst(crate.rootModule_, path, 0, ignoreLast, outPath);
            } else if (e.crate.c_str()[0] == '=') {
                const char* n = e.crate.c_str() + 1;
                if (n == crate.crateNameSet) {
                    return getModuleAst(crate.rootModule_, path, 0, ignoreLast, outPath);
                }
                auto ecIt = settings.implicitCrates.find(n);
                if (ecIt == settings.implicitCrates.end()) {
                    return ResolveModuleRef();
                }
                if (ecIt->second == "") {
                    return getModuleAst(crate.rootModule_, path, 0, ignoreLast, outPath);
                }
                auto ecIt2 = crate.externCrates.find(ecIt->second);
                if (ecIt2 == crate.externCrates.end()) {
                    DEBUG(StringView("Crate ") << ecIt->second << StringView(" not found"));
                    return ResolveModuleRef();
                }
                if (outPath) {
                    *outPath = ASTAbsolutePath(ecIt->second, {});
                }
                return getModuleHir(ecIt2->second.hir->rootModule, path, 0, ignoreLast, outPath);
            } else {
                auto ecIt = crate.externCrates.find(e.crate);
                if (ecIt == crate.externCrates.end()) {
                    DEBUG(StringView("Crate ") << e.crate << StringView(" not found"));
                    return ResolveModuleRef();
                }
                if (outPath) {
                    *outPath = ASTAbsolutePath(e.crate, {});
                }
                return getModuleHir(ecIt->second.hir->rootModule, path, 0, ignoreLast, outPath);
            }
            break;
        }
    }
    UNREACHABLE();
}

auto ResolveState::getModuleForMacro(const ASTPath& basePath, const ASTPath& path, ASTAbsolutePath* outPath) -> ResolveModuleRef {
    auto mod = getModule(basePath, path, /*ignore_last=*/true, outPath);
    if (!mod.is_None() || !path.cls.is_Relative() || path.nodes().size() < 2) {
        return mod;
    }

    const auto& crateAlias = path.nodes().front().name();
    auto implicitIt = settings.implicitCrates.find(crateAlias);
    if (implicitIt == settings.implicitCrates.end()) {
        return mod;
    }
    if (implicitIt->second == "") {
        return getModuleAst(crate.rootModule_, path, 1, /*ignore_last=*/true, outPath);
    }

    ASSERT_BUG(sp, crate.externCrates.count(implicitIt->second), StringView("Crate \"") << implicitIt->second << StringView("\" not loaded (for \"") << crateAlias << StringView("\")"));
    const auto& externalCrate = crate.externCrates.at(implicitIt->second);
    if (outPath) {
        *outPath = ASTAbsolutePath(implicitIt->second, {});
    }
    return getModuleHir(externalCrate.hir->rootModule, path, 1, /*ignore_last=*/true, outPath);
}

auto ResolveState::getModuleAst(const ASTModule& startMod, const ASTPath& path, size_t startOffset, bool ignoreLast, ASTAbsolutePath* outPath) -> ResolveModuleRef {
    TRACE_FUNCTION_F(StringView("start_offset=") << startOffset << StringView(", ignore_last=") << ignoreLast);
    const ASTModule* mod = &startMod;
    ASSERT_BUG(Span(), path.nodes().size() >= (ignoreLast ? 1 : 0), StringView("") << path);

    for (size_t idx = startOffset; idx < path.nodes().size() - (ignoreLast ? 1 : 0); idx++) {
        const auto& name = path.nodes()[idx].name();

        auto res = findItem(*mod, name, ResolveNamespace::Namespace, outPath);
        if (res.is_None()) {
            DEBUG(StringView("Unable to find ") << name << StringView(" in module ") << mod->path() << StringView(" for ") << path);
            return ResolveModuleRef();
        }
        const auto& r = res.as_Namespace();
        switch (r.tag()) {
            case ResolveItemRefType::TAG_None: {
                DEBUG(StringView("Not found (Namespace::None)"));
                return ResolveModuleRef();
            }
            case ResolveItemRefType::TAG_Ast: {
                auto& e = r.as_Ast();
                if (e->is_Module()) {
                    mod = &e->as_Module();
                } else if (const auto* i = e->opt_Crate()) {
                    if (outPath) {
                        *outPath = ASTAbsolutePath(i->name, {});
                    }
                    if (i->name == "") {
                        return getModuleAst(crate.rootModule_, path, idx + 1, ignoreLast, outPath);
                    } else {
                        ASSERT_BUG(sp, crate.externCrates.count(i->name) != 0, StringView("Cannot find crate `") << i->name << StringView("`"));
                        return getModuleHir(crate.externCrates.at(i->name).hir->rootModule, path, idx + 1, ignoreLast, outPath);
                    }
                } else {
                    DEBUG(StringView("Found ") << e->tagStr() << StringView(", not module"));
                    return ResolveModuleRef();
                }
                break;
            }
            case ResolveItemRefType::TAG_AstRoot: {
                auto& e = r.as_AstRoot();
                mod = e;
                break;
            }
            case ResolveItemRefType::TAG_Hir: {
                auto& e = r.as_Hir();
                if (const auto* i = e->opt_Module()) {
                    return getModuleHir(*i, path, idx + 1, ignoreLast, outPath);
                } else {
                    DEBUG(StringView("Found HIR ") << e->tagStr() << StringView(", not module"));
                    return ResolveModuleRef();
                }
                break;
            }
            case ResolveItemRefType::TAG_HirRoot: {
                auto& e = r.as_HirRoot();
                if (outPath) {
                    outPath->crate = e->crateName;
                    outPath->nodes.clear();
                }
                return getModuleHir(e->rootModule, path, idx + 1, ignoreLast, outPath);
            }
        }
    }
    if (outPath) {
        *outPath = mod->path();
    }
    return ResolveModuleRef(mod);
}

auto ResolveState::getModuleHir(const HIRModule& startMod, const ASTPath& path, size_t startOffset, bool ignoreLast, ASTAbsolutePath* outPath) -> ResolveModuleRef {
    TRACE_FUNCTION_F(StringView("path=") << path << StringView(", start_offset=") << startOffset << StringView(", ignore_last=") << ignoreLast);
    const HIRModule* mod = &startMod;
    ASSERT_BUG(Span(), path.nodes().size() >= (ignoreLast ? 1 : 0), StringView("") << path);
    for (size_t i = startOffset; i < path.nodes().size() - (ignoreLast ? 1 : 0); i++) {
        const auto& name = path.nodes()[i].name();
        auto it = mod->modItems.find(name);
        if (it == mod->modItems.end() || !it->second->publicity.isGlobal()) {
            DEBUG(name << StringView(" Not Found"));
            return ResolveModuleRef();
        }
        const auto* ti = &it->second->ent;
        if (const auto* imp = ti->opt_Import()) {
            ASSERT_BUG(sp, crate.externCrates.count(imp->path.crateName()), StringView("Crate ") << imp->path.crateName() << StringView(" not loaded"));
            const auto& extCrate = *crate.externCrates.at(imp->path.crateName()).hir;
            if (imp->path.components().empty()) {
                mod = &extCrate.rootModule;
                continue;
            }
            if (outPath) {
                *outPath = spToAp(imp->path);
            }
            ti = &extCrate.getTypeitemByPath(sp, imp->path, /*ignore_crate*/ true, /*ignore_last*/ false);
        } else {
            if (outPath) {
                outPath->nodes.push_back(name);
            }
        }
        switch ((*ti).tag()) {
            default:
                DEBUG(name << StringView(" Not Module, instead ") << ti->tagStr());
                return ResolveModuleRef();
            case HIRTypeItem::TAG_Module: {
                auto& m = (*ti).as_Module();
                mod = &m;
                break;
            }
        }
    }
    if (outPath) {
        ASSERT_BUG(sp, outPath->crate != "", StringView("Invalid HIR output path - crate name not set"));
    }
    return ResolveModuleRef(mod);
}

auto ResolveState::getModByTruePath(const std::vector<ASTPathNode>& baseNodes, size_t len) -> const ASTModule& {
    const ASTModule* mod = &crate.rootModule_;
    for (size_t i = 0; i < len; i++) {
        const auto& tgtName = baseNodes[i].name();
        if (tgtName.c_str()[0] == '#') {
            auto idx = strtol(tgtName.c_str() + 1, nullptr, 10);
            mod = &*mod->anonMods()[idx];
            continue;
        }
        const ASTModule* nextMod = nullptr;
        for (const auto& i : mod->items) {
            if (const auto* m = i->data.opt_Module()) {
                if (i->name == tgtName) {
                    nextMod = m;
                    break;
                }
            }
        }
        if (!nextMod) {
            BUG(sp, StringView("Unable to find component `") << tgtName << StringView("` of [") << baseNodes << StringView("] in module ") << mod->path());
        }
        mod = nextMod;
    }
    return *mod;
}

auto ResolveState::matchingNamespace(const ASTItem& i, ResolveNamespace ns) -> bool {
    if (i.isDead()) {
        return false;
    }
    switch (i.tag()) {
        case ASTItem::TAG_Crate:
        case ASTItem::TAG_Module:
        case ASTItem::TAG_Type:
        case ASTItem::TAG_Enum:
        case ASTItem::TAG_Union:
        case ASTItem::TAG_Trait:
        case ASTItem::TAG_TraitAlias:
            return ns == ResolveNamespace::Namespace;
        case ASTItem::TAG_Struct:
            return ns == ResolveNamespace::Namespace || (ns == ResolveNamespace::Value && !i.as_Struct().data.is_Struct());
        case ASTItem::TAG_Function:
        case ASTItem::TAG_Static:
            return ns == ResolveNamespace::Value;
        case ASTItem::TAG_Macro:
            return ns == ResolveNamespace::Macro;
        case ASTItem::TAG_NegImpl:
        case ASTItem::TAG_Impl:
        case ASTItem::TAG_ExternBlock:
        case ASTItem::TAG_Use:
        case ASTItem::TAG_MacroInv:
        case ASTItem::TAG_GlobalAsm:
        case ASTItem::TAG_None:
            return false;
    }
    UNREACHABLE();
}

auto ResolveState::findItem(const ASTModule& mod, const RcString& name, ResolveNamespace ns, ASTAbsolutePath* outPath) -> ResolveItemRef {
    TRACE_FUNCTION_F(StringView("Looking for ") << name << StringView(" in ") << mod.path() << StringView(" (ns=") << ns << StringView(")"));
    if (mod.indexPopulated) {
        TODO(sp, StringView("Look up in index"));
    }

    auto guardEnt = std::make_pair(&mod, name);
    bool visitUse = true;
    if (std::count(antirecurseStack.begin(), antirecurseStack.end(), guardEnt) > 0) {
        DEBUG(StringView("Recursion detected, not looking at `use` statements in ") << mod.path());
        visitUse = false;
    }

    antirecurseStack.push_back(std::move(guardEnt));
    STD_DEFER {
        antirecurseStack.pop_back();
    };

    if (ns == ResolveNamespace::Macro) {
        for (const auto& i : mod.macros()) {
            DEBUG(StringView("> MACRO ") << i.name);
            if (i.name == name) {
                DEBUG(StringView("Found in ast (macro)"));
                if (outPath) {
                    outPath->nodes.push_back(name);
                }
                return ResolveItemRef::make_Macro(&*i.data);
            }
        }
        for (const auto& mac : reverse(mod.macroImports)) {
            if (mac.ref.is_None()) {
                continue;
            }
            if (mac.name == name) {
                // TODO: What about macro re-exports a builtin?
                DEBUG(StringView("Found in ast (macro import) - ") << mac.path);
                if (outPath) {
                    *outPath = mac.path;
                }
                switch (mac.ref.tag()) {
                    case MacroRef::TAG_None: {
                        BUG(sp, StringView("macro_imports_res had a None entry"));
                        break;
                    }
                    case MacroRef::TAG_MacroRules: {
                        auto& me = mac.ref.as_MacroRules();
                        return ResolveItemRefMacro(me);
                    }
                    case MacroRef::TAG_BuiltinProcMacro: {
                        auto& me = mac.ref.as_BuiltinProcMacro();
                        return ResolveItemRefMacro(me);
                    }
                    case MacroRef::TAG_ExternalProcMacro: {
                        auto& me = mac.ref.as_ExternalProcMacro();
                        return ResolveItemRefMacro(me);
                    }
                }
            }
        }
    }

    for (const auto& i : mod.items) {
        switch (i->cachedCfg) {
            case ASTCachedCfg::Unknown:
                i->cachedCfg = checkCfgAttrs(settings, i->attrs) ? ASTCachedCfg::Yes : ASTCachedCfg::No;
            case ASTCachedCfg::Yes:
            case ASTCachedCfg::No:
                if (i->cachedCfg == ASTCachedCfg::No) {
                    continue;
                }
                break;
        }
        if (matchingNamespace(i->data, ns) && i->name == name) {
            if (outPath) {
                outPath->nodes.push_back(name);
            }
            DEBUG(StringView("Found in ast (") << i->data.tagStr() << StringView(")"));
            switch (ns) {
                case ResolveNamespace::Macro:
                    if (const auto* mac = i->data.opt_Macro()) {
                        if (i->attrs.get("rustc_builtin_macro")) {
                            auto* rv = ExpandFindProcMacro(crate.wb, name);
                            if (rv) {
                                return ResolveItemRefMacro(rv);
                            }
                            // HACK: Ignore, as there's references to the `Debug` macro... but trustme doesn't do things that way
                        }
                        return ResolveItemRefMacro(&**mac);
                    }
                    DEBUG(StringView("- Ignoring macro"));
                    break;
                case ResolveNamespace::Namespace:
                    return ResolveItemRefType(&i->data);
                case ResolveNamespace::Value:
                    return ResolveItemRefValue(&i->data);
            }
        }

        if (const auto* useStmt = i->data.opt_Use()) {
            if (!visitUse) {
                continue;
            }
            for (const auto& e : useStmt->entries) {
                if (e.name == name) {
                    DEBUG(StringView("Use ") << e.name << StringView(" := ") << e.path);
                    if (e.path.cls.is_Absolute() && e.path.cls.as_Absolute().crate == CRATE_BUILTINS) {
                        const auto& pe = e.path.cls.as_Absolute();
                        if (ns == ResolveNamespace::Macro) {
                            if (outPath) {
                                outPath->crate = pe.crate;
                                outPath->nodes = makeVec1<RcString>(RcString(pe.nodes.front().name()));
                            }
                            return ResolveItemRefMacro(ExpandFindProcMacro(crate.wb, pe.nodes.front().name()));
                        }
                    }
                    if (e.path.cls.is_Absolute() && e.path.cls.as_Absolute().nodes.empty()) {
                        if (ns == ResolveNamespace::Namespace) {
                            ASTAbsolutePath tmp;
                            auto tgtMod = this->getModule(mod.path(), e.path, false, &tmp);
                            switch (tgtMod.tag()) {
                                case ResolveModuleRef::TAG_Ast: {
                                    auto& modPtr = tgtMod.as_Ast();
                                    if (outPath) {
                                        *outPath = tmp;
                                    }
                                    return ResolveItemRefType::make_AstRoot(modPtr);
                                }
                                case ResolveModuleRef::TAG_Hir: {
                                    if (outPath) {
                                        *outPath = tmp;
                                    }
                                    return ResolveItemRefType::make_HirRoot(&*crate.externCrates.at(tmp.crate).hir);
                                }
                                case ResolveModuleRef::TAG_ImplicitPrelude: {
                                    TODO(sp, StringView("ImplicitPrelude?"));
                                    break;
                                }
                                case ResolveModuleRef::TAG_None: {
                                    break;
                                }
                            }
                        }
                        continue;
                    }

                    const auto& itemName = e.path.nodes().back().name();
                    auto tgtMod = this->getModule(mod.path(), e.path, true, outPath);

                    DEBUG(tgtMod.tagStr());
                    switch (tgtMod.tag()) {
                        case ResolveModuleRef::TAG_Ast: {
                            auto& modPtr = tgtMod.as_Ast();
                            auto rv = this->findItem(*modPtr, itemName, ns, outPath);
                            if (!rv.is_None()) {
                                DEBUG(StringView("Found in AST use"));
                                return rv;
                            }
                            break;
                        }
                        case ResolveModuleRef::TAG_Hir: {
                            auto& modPtr = tgtMod.as_Hir();
                            auto rv = this->findItemHir(*modPtr, itemName, ns, outPath);
                            if (!rv.is_None()) {
                                DEBUG(StringView("Found in HIR use"));
                                return rv;
                            }
                            break;
                        }
                        case ResolveModuleRef::TAG_ImplicitPrelude: {
                            if (ns == ResolveNamespace::Namespace) {
                                auto ecIt = settings.implicitCrates.find(itemName);
                                if (ecIt != settings.implicitCrates.end()) {
                                    if (outPath) {
                                        outPath->crate = ecIt->second;
                                        outPath->nodes.clear();
                                    }
                                    return ResolveItemRefType(&*crate.externCrates.at(ecIt->second).hir);
                                }
                                TODO(sp, StringView("ImplicitPrelude?"));
                            }
                            break;
                        }
                        case ResolveModuleRef::TAG_None: {
                            break;
                        }
                    }
                }
            }
        }
    }
    for (const auto& i : mod.items) {
        if (const auto* useStmt = i->data.opt_Use()) {
            if (!visitUse) {
                continue;
            }
            for (const auto& e : useStmt->entries) {
                if (e.name == "") {
                    DEBUG(StringView("Glob use ") << e.path);
                    auto srcMod = this->getModule(mod.path(), e.path, /*ignore_last=*/false, outPath);
                    switch (srcMod.tag()) {
                        case ResolveModuleRef::TAG_None: {
                            auto& _ = srcMod.as_None();
                            DEBUG(StringView("Unable to find ") << e.path);
                            break;
                        }
                        case ResolveModuleRef::TAG_ImplicitPrelude: {
                            TODO(sp, StringView("ImplicitPrelude? ") << e.path);
                            break;
                        }
                        case ResolveModuleRef::TAG_Ast: {
                            auto& sm = srcMod.as_Ast();
                            auto rv = findItem(*sm, name, ns, outPath);
                            if (!rv.is_None()) {
                                DEBUG(StringView("Found in AST glob"));
                                return rv;
                            }
                            break;
                        }
                        case ResolveModuleRef::TAG_Hir: {
                            auto& sm = srcMod.as_Hir();
                            auto rv = this->findItemHir(*sm, name, ns, outPath);
                            if (!rv.is_None()) {
                                DEBUG(StringView("Found HIR glob"));
                                return rv;
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
    if (mod.isAnon()) {
        DEBUG(StringView("Recurse to parent"));
        const ASTModule* m = &crate.rootModule_;
        for (size_t i = 0; i < mod.path().nodes.size() - 1; i++) {
            auto& tgtName = mod.path().nodes[i];
            if (tgtName.c_str()[0] == '#') {
                auto idx = strtol(tgtName.c_str() + 1, nullptr, 10);
                m = &*m->anonMods()[idx];
            } else {
                m = &as_Namespace(this->findItem(*m, tgtName, ResolveNamespace::Namespace)).as_Ast()->as_Module();
            }
        }
        return findItem(*m, name, ns, outPath);
    }
    DEBUG(StringView("Not found"));
    return ResolveItemRef::make_None({});
}

auto ResolveState::findItemHir(const HIRModule& mod, const RcString& itemName, ResolveNamespace ns, ASTAbsolutePath* outPath, const HIRSimplePath* visPathP) -> ResolveItemRef {
    const auto& visPath = visPathP ? *visPathP : HIRSimplePath();
    TRACE_FUNCTION_F(itemName);
    if (outPath) {
        ASSERT_BUG(sp, outPath->crate != "", StringView("Crate not filled"));
    }

    struct H {
        static const HIRCrate& getCrate(const Span& sp, const ASTCrate& crate, const HIRSimplePath& p) {
            return *crate.externCrates.at(p.crateName()).hir;
        }

        static const HIRModule& getModForHirPath(const Span& sp, const ASTCrate& crate, const HIRSimplePath& p) {
            const auto& hirCrate = *crate.externCrates.at(p.crateName()).hir;
            return hirCrate.getModByPath(sp, p, /*ignore_last*/ true, /*ingore_crate*/ true);
        }
    };

    switch (ns) {
        case ResolveNamespace::Namespace: {
            auto it = mod.modItems.find(itemName);
            if (it != mod.modItems.end() && it->second->publicity.isVisible(visPath)) {
                DEBUG(StringView("Found `") << itemName << StringView("` in HIR namespace"));
                const HIRTypeItem* ti;
                if (const auto* p = it->second->ent.opt_Import()) {
                    if (outPath) {
                        *outPath = spToAp(p->path);
                    }
                    const auto& extCrate = H::getCrate(sp, crate, p->path);
                    if (p->path.components().empty()) {
                        return ResolveItemRefType(&extCrate);
                    }
                    ti = &extCrate.getTypeitemByPath(sp, p->path, true);
                } else {
                    if (outPath) {
                        outPath->nodes.push_back(itemName);
                    }
                    ti = &it->second->ent;
                }
                ASSERT_BUG(sp, !ti->is_Import(), StringView("Recursive namespace import in HIR: ") << it->second->ent.as_Import().path << StringView(" pointed to ") << ti->as_Import().path);
                return ResolveItemRefType(ti);
            }
        } break;
        case ResolveNamespace::Value: {
            auto it = mod.valueItems.find(itemName);
            if (it != mod.valueItems.end() && it->second->publicity.isVisible(visPath)) {
                DEBUG(StringView("Found `") << itemName << StringView("` in HIR value"));
                const HIRValueItem* vi;
                if (const auto* p = it->second->ent.opt_Import()) {
                    if (outPath) {
                        *outPath = spToAp(p->path);
                    }
                    vi = &H::getCrate(sp, crate, p->path).getValitemByPath(sp, p->path, true);
                } else {
                    if (outPath) {
                        outPath->nodes.push_back(itemName);
                    }
                    vi = &it->second->ent;
                }
                ASSERT_BUG(sp, !vi->is_Import(), StringView("Recursive value import in HIR: ") << it->second->ent.as_Import().path << StringView(" pointed to ") << vi->as_Import().path);
                return ResolveItemRefValue(vi);
            }
        } break;
        case ResolveNamespace::Macro: {
            auto it = mod.macroItems.find(itemName);
            if (it == mod.macroItems.end()) {
                DEBUG(StringView("Did not find `") << itemName << StringView("` in HIR macro"));
            } else if (!it->second->publicity.isVisible(visPath)) {
                DEBUG(StringView("Found `") << itemName << StringView("` in HIR macro - but not public, ignoring"));
            } else {
                DEBUG(StringView("Found `") << itemName << StringView("` in HIR macro"));
                const HIRMacroItem* mi;
                if (const auto* p = it->second->ent.opt_Import()) {
                    if (outPath) {
                        *outPath = spToAp(p->path);
                    }

                    struct H2 {
                        static ResolveItemRefMacro getBuiltin(const WireBoard& wb, const Span& sp, const RcString& name) {
                            // TODO: What if it's a derive? Or it's an attribute
                            if (auto* pm = ExpandFindProcMacro(wb, name)) {
                                return ResolveItemRefMacro(pm);
                            }
                            //    TODO(sp, StringView("Resolve HIR import to decorator"));

                            DEBUG(StringView("Import of builtins: Not found"));
                            return {};
                        }
                    };

                    if (p->path.crateName() == CRATE_BUILTINS) {
                        auto v = H2::getBuiltin(crate.wb, sp, p->path.components().back());
                        if (v.is_None()) {
                            break;
                        }
                        return v;
                    }
                    mi = &H::getCrate(sp, crate, p->path).getMacroitemByPath(sp, p->path, true);
                    if (const auto* p = mi->opt_Import()) {
                        if (p->path.crateName() == CRATE_BUILTINS) {
                            auto v = H2::getBuiltin(crate.wb, sp, p->path.components().back());
                            if (v.is_None()) {
                                break;
                            }
                            return v;
                        }
                    }
                } else {
                    mi = &it->second->ent;
                    if (outPath) {
                        outPath->nodes.push_back(itemName);
                    }
                }
                switch ((*mi).tag()) {
                    case HIRMacroItem::TAG_Import: {
                        auto& me = (*mi).as_Import();
                        BUG(sp, StringView("Recursive macro import in HIR: ") << it->second->ent.as_Import().path << StringView(" pointed to ") << me.path);
                        break;
                    }
                    case HIRMacroItem::TAG_MacroRules: {
                        auto& me = (*mi).as_MacroRules();
                        return ResolveItemRefMacro(&*me);
                    }
                    case HIRMacroItem::TAG_ProcMacro: {
                        auto& me = (*mi).as_ProcMacro();
                        return ResolveItemRefMacro(&me);
                    }
                }
            }
        } break;
    }

    return ResolveItemRef::make_None({});
}

template <>
void stl::output<ZeroCopyOutput, ResolveNamespace>(ZeroCopyOutput& os, ResolveNamespace ns) {
    switch (ns) {
        case ResolveNamespace::Namespace:
            os << StringView("Namespace");
            return;
        case ResolveNamespace::Value:
            os << StringView("Value");
            return;
        case ResolveNamespace::Macro:
            os << StringView("Macro");
            return;
    }
    os << StringView("?");
    return;
}
