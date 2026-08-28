#include "resolve_common.h"

#include "ast_ast.h"
#include "hir_hir.h"
#include "ast_expr.h"
#include "settings.h"
#include "ast_crate.h"
#include "expand_cfg.h"
#include "synext_macro.h"
#include "resolve_main_bindings.h"

#include <std/alg/defer.h>

#include <span> // std::span

::std::ostream& operator<<(::std::ostream& os, ResolveNamespace ns) {
    switch (ns) {
        case ResolveNamespace::Namespace:
            return os << "Namespace";
        case ResolveNamespace::Value:
            return os << "Value";
        case ResolveNamespace::Macro:
            return os << "Macro";
    }
    return os << "?";
}

namespace {

    ASTAbsolutePath spToAp(const HIRSimplePath& sp) {
        return ASTAbsolutePath(sp.crateName(), sp.componentsVec());
    }

    ResolveItemRefType as_Namespace(ResolveItemRef ir) {
        if (ir.is_None()) {
            return ResolveItemRefType::make_None({});
        }
        return std::move(ir.as_Namespace());
    }

    struct ResolveState {
        const Span& sp;
        const Settings& settings;
        const ASTCrate& crate;

        typedef std::pair<const ASTModule*, RcString> antirecurseStackEntT;
        std::vector<antirecurseStackEntT> antirecurseStack;

        ResolveState(const Span& span, const Settings& settings, const ASTCrate& crate);

        /// <summary>
        /// Obtain a reference to the specified module
        /// </summary>
        ResolveModuleRef getModule(const ASTPath& basePath, const ASTPath& path, bool ignoreLast, ASTAbsolutePath* outPath, bool ignoreHygiene = false);

        ResolveModuleRef getModuleForMacro(const ASTPath& basePath, const ASTPath& path, ASTAbsolutePath* outPath);

        ResolveModuleRef getModuleAst(const ASTModule& startMod, const ASTPath& path, size_t startOffset, bool ignoreLast, ASTAbsolutePath* outPath);

        ResolveModuleRef getModuleHir(const HIRModule& startMod, const ASTPath& path, size_t startOffset, bool ignoreLast, ASTAbsolutePath* outPath);

        const ASTModule& getModByTruePath(const std::vector<ASTPathNode>& baseNodes, size_t len);

        static bool matchingNamespace(const ASTItem& i, ResolveNamespace ns);

        ResolveItemRef findItem(const ASTModule& mod, const RcString& name, ResolveNamespace ns, ASTAbsolutePath* outPath = nullptr)
            //ResolveModuleRef get_source_module_for_name(const AST::Module& mod, const RcString& name, ResolveNamespace ns, ::AST::AbsolutePath* out_path=nullptr)
            ;

        /// Locate the named item in HIR (resolving `Import` references too)
        ResolveItemRef findItemHir(const HIRModule& mod, const RcString& itemName, ResolveNamespace ns, ASTAbsolutePath* outPath = nullptr, const HIRSimplePath* visPathP = nullptr);
    };
}

// TODO: Function that turns a relative path into a canonical absolute path to the containing module
// - This should check if the index has been populated, and use it if present.
// - NOTE: Can only go to the containing module, not to the item itself - `use` can end up importing disparate paths for all three namespaces.
ResolveModuleRef ResolveLookupGetModule(const Span& sp, const Settings& settings, const ASTCrate& crate, const ASTPath& basePath, ASTPath path, bool ignoreLast, ASTAbsolutePath* outPath) {
    ResolveState rs(sp, settings, crate);

    return rs.getModule(basePath, path, ignoreLast, outPath);
}

ResolveItemRefMacro ResolveLookupMacro(const Span& span, const Settings& settings, const ASTCrate& crate, const ASTPath& basePath, ASTPath path, ASTAbsolutePath* outPath) {
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
            // This isn't a macro, so return `None`
            return ResolveItemRefMacro::make_None({});
        }
        case ResolveModuleRef::TAG_None: {
            return ResolveItemRefMacro::make_None({});
        }
    }
    // Technically a bug to reach this point.
    return ResolveItemRefMacro::make_None({});
}

/// Returns the source module for the specified name
// NOTE: Name resolution
ResolveModuleRef ResolveLookupGetModuleForName(const Span& sp, const Settings& settings, const ASTCrate& crate, const ASTPath& basePath, const ASTPath& path, ResolveNamespace ns, ASTAbsolutePath* outPath) {
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
                BUG(sp, "Unable to find " << path << " (starting from " << basePath << ")");
            }

            TODO(sp, "");
            break;
        }
        case ResolveModuleRef::TAG_Hir: {
            // If `get_module` provided a HIR module, then this is right?
            // - What if it's an alias? (not critical)
            return mod;
        }
        case ResolveModuleRef::TAG_ImplicitPrelude: {
            return mod;
        }
        case ResolveModuleRef::TAG_None: {
            BUG(sp, "Unable to find " << path << " (starting from " << basePath << ")");
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
    const auto& baseNodes = basePath.nodes();
    switch (path.cls.tag()) {
        case ASTPathClass::TAG_Invalid: {
            // Should never happen
            BUG(sp, "Invalid path class encountered");
            break;
        }
        case ASTPathClass::TAG_Local: {
            // Wait, how is this already known?
            BUG(sp, "Local path class in use statement");
            break;
        }
        case ASTPathClass::TAG_UFCS: {
            // Wait, how is this already known?
            BUG(sp, "UFCS path class in use statement");
            break;
        }
        case ASTPathClass::TAG_Relative: {
            auto& e = path.cls.as_Relative();
            ASSERT_BUG(sp, !e.nodes.empty(), "");
            if (!ignoreHygiene && e.hygiene.hasModPath()) {
                const auto& mp = e.hygiene.modPath();
                if (mp.crate != "") {
                    ASSERT_BUG(sp, this->crate.externCrates.count(mp.crate), "Crate not loaded for " << mp);
                    const auto& crate = this->crate.externCrates.at(mp.crate);
                    const HIRModule* mod = &crate.hir->rootModule;
                    for (const auto& n : mp.ents) {
                        ASSERT_BUG(sp, mod->modItems.count(n), "Node `" << n << "` missing in path " << mp);
                        const auto& i = *mod->modItems.at(n);
                        ASSERT_BUG(sp, i.ent.is_Module(), "Node `" << n << "` not a module in path " << mp);
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
                    auto ecIt = settings.implicitCrates.find(name);
                    if (ecIt != settings.implicitCrates.end()) {
                        return ResolveModuleRef::make_ImplicitPrelude({});
                    }
                }

                const auto& currentMod = this->getModByTruePath(baseNodes, baseNodes.size());
                if (outPath) {
                    *outPath = currentMod.path();
                }
                return ResolveModuleRef(&currentMod);
            }
            const auto& name = e.nodes.front().name();
            // Look up in stack of anon modules
            size_t i = 0;
            do {
                // Get a reference to the module, given the current path
                const auto& startMod = this->getModByTruePath(baseNodes, baseNodes.size() - i);

                // Find the top of the path in that namespace
                auto realMod = as_Namespace(this->findItem(startMod, name, ResolveNamespace::Namespace, outPath));
                switch (realMod.tag()) {
                    case ResolveItemRefType::TAG_Ast: {
                        auto& iData = realMod.as_Ast();
                        // TODO: What about an enum?
                        switch ((*iData).tag()) {
                            default: {
                                // Ignore, keep going
                            } break;
                            case ASTItem::TAG_Crate: {
                                auto& c = (*iData).as_Crate();
                                if (outPath) {
                                    *outPath = ASTAbsolutePath(c.name, {});
                                }
                                if (c.name == "") {
                                    return getModuleAst(crate.rootModule_, path, 1, ignoreLast, outPath);
                                }
                                ASSERT_BUG(sp, crate.externCrates.count(c.name) > 0, "Unable to find crate `" << c.name << "`");
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
                        ASSERT_BUG(sp, !iEntPtr->is_Import(), "");
                        if (iEntPtr->is_Enum()) {
                            return ResolveModuleRef();
                        }
                        if (iEntPtr->is_Trait()) {
                            // A trait is not a module, but an associated item
                            // of one can be imported; the caller looks the
                            // name up in the trait itself.
                            return ResolveModuleRef();
                        }

                        //}
                        ASSERT_BUG(sp, iEntPtr->is_Module(), "Expected Module, got " << iEntPtr->tagStr() << " for " << name << " in [" << baseNodes << "]");
                        return getModuleHir(iEntPtr->as_Module(), path, 1, ignoreLast, outPath);
                        //}
                        break;
                    }
                    case ResolveItemRefType::TAG_None: {
                        // Not found in this module, keep searching
                        break;
                    }
                }

                i += 1;
            } while (i < baseNodes.size() && baseNodes[baseNodes.size() - i].name().c_str()[0] == '#');

            // If not found, look for an implicit crate allowed in this edition.
            if (crate.edition >= ASTEdition::Rust2018 || name == "core" || name == "std") {
                auto ecIt = settings.implicitCrates.find(name);
                if (ecIt != settings.implicitCrates.end()) {
                    if (ecIt->second == "") {
                        // This crate!
                        return getModuleAst(crate.rootModule_, path, 1, ignoreLast, outPath);
                    } else {
                        ASSERT_BUG(sp, crate.externCrates.count(ecIt->second), "Crate \"" << ecIt->second << "\" not loaded (for \"" << ecIt->first << "\")");
                        const auto& ec = crate.externCrates.at(ecIt->second);
                        if (outPath) {
                            *outPath = ASTAbsolutePath(ecIt->second, {});
                        }
                        return getModuleHir(ec.hir->rootModule, path, 1, ignoreLast, outPath);
                    }
                }
            }
            return ResolveModuleRef();
        }
        case ASTPathClass::TAG_Self: {
            // Look up within the non-anon module
            size_t i = 0;
            while (i < baseNodes.size() && baseNodes[baseNodes.size() - i - 1].name().c_str()[0] == '#') {
                i += 1;
            }
            const auto& startMod = this->getModByTruePath(baseNodes, baseNodes.size() - i);
            return getModuleAst(startMod, path, 0, ignoreLast, outPath);
        }
        case ASTPathClass::TAG_Super: {
            // Pop current non-anon module, then look up in anon modules
            size_t i = 0;
            while (i < baseNodes.size() && baseNodes[baseNodes.size() - i - 1].name().c_str()[0] == '#') {
                i += 1;
            }
            i += 1;
            ASSERT_BUG(sp, i <= baseNodes.size(), "");
            const auto& startMod = this->getModByTruePath(baseNodes, baseNodes.size() - i);
            return getModuleAst(startMod, path, 0, ignoreLast, outPath);
        }
        case ASTPathClass::TAG_Absolute: {
            auto& e = path.cls.as_Absolute();
            if (e.crate == "" || e.crate == crate.crateNameReal) {
                return getModuleAst(crate.rootModule_, path, 0, ignoreLast, outPath);
            }
            // 2018 `::cratename::` paths
            else if (e.crate.c_str()[0] == '=') {
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
                    return ResolveModuleRef();
                }
                if (outPath) {
                    *outPath = ASTAbsolutePath(ecIt->second, {});
                }
                return getModuleHir(ecIt2->second.hir->rootModule, path, 0, ignoreLast, outPath);
            } else {
                // HIR lookup (different)
                auto ecIt = crate.externCrates.find(e.crate);
                if (ecIt == crate.externCrates.end()) {
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

    // Qualified macro paths have used the extern prelude since their
    // introduction, including in the 2015 edition. Other path kinds
    // retain the edition-dependent lookup performed by getModule.
    const auto& crateAlias = path.nodes().front().name();
    auto implicitIt = settings.implicitCrates.find(crateAlias);
    if (implicitIt == settings.implicitCrates.end()) {
        return mod;
    }
    if (implicitIt->second == "") {
        return getModuleAst(crate.rootModule_, path, 1, /*ignore_last=*/true, outPath);
    }

    ASSERT_BUG(sp, crate.externCrates.count(implicitIt->second), "Crate \"" << implicitIt->second << "\" not loaded (for \"" << crateAlias << "\")");
    const auto& externalCrate = crate.externCrates.at(implicitIt->second);
    if (outPath) {
        *outPath = ASTAbsolutePath(implicitIt->second, {});
    }
    return getModuleHir(externalCrate.hir->rootModule, path, 1, /*ignore_last=*/true, outPath);
}

auto ResolveState::getModuleAst(const ASTModule& startMod, const ASTPath& path, size_t startOffset, bool ignoreLast, ASTAbsolutePath* outPath) -> ResolveModuleRef {
    const ASTModule* mod = &startMod;
    ASSERT_BUG(Span(), path.nodes().size() >= (ignoreLast ? 1 : 0), "" << path);

    for (size_t idx = startOffset; idx < path.nodes().size() - (ignoreLast ? 1 : 0); idx++) {
        const auto& name = path.nodes()[idx].name();

        auto res = findItem(*mod, name, ResolveNamespace::Namespace, outPath);
        if (res.is_None()) {
            return ResolveModuleRef();
        }
        const auto& r = res.as_Namespace();
        switch (r.tag()) {
            case ResolveItemRefType::TAG_None: {
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
                        ASSERT_BUG(sp, crate.externCrates.count(i->name) != 0, "Cannot find crate `" << i->name << "`");
                        return getModuleHir(crate.externCrates.at(i->name).hir->rootModule, path, idx + 1, ignoreLast, outPath);
                    }
                } else {
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
    const HIRModule* mod = &startMod;
    ASSERT_BUG(Span(), path.nodes().size() >= (ignoreLast ? 1 : 0), "" << path);
    for (size_t i = startOffset; i < path.nodes().size() - (ignoreLast ? 1 : 0); i++) {
        const auto& name = path.nodes()[i].name();
        // Find the module for this node
        auto it = mod->modItems.find(name);
        if (it == mod->modItems.end() || !it->second->publicity.isGlobal()) {
            return ResolveModuleRef();
        }
        const auto* ti = &it->second->ent;
        if (const auto* imp = ti->opt_Import()) {
            ASSERT_BUG(sp, crate.externCrates.count(imp->path.crateName()), "Crate " << imp->path.crateName() << " not loaded");
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
                return ResolveModuleRef();
            case HIRTypeItem::TAG_Module: {
                auto& m = (*ti).as_Module();
                mod = &m;
                break;
            }
        }
    }
    if (outPath) {
        ASSERT_BUG(sp, outPath->crate != "", "Invalid HIR output path - crate name not set");
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
            BUG(sp, "Unable to find component `" << tgtName << "` of [" << baseNodes << "] in module " << mod->path());
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

auto ResolveState::findItem(const ASTModule& mod, const RcString& name, ResolveNamespace ns, ASTAbsolutePath* outPath)
    //ResolveModuleRef get_source_module_for_name(const AST::Module& mod, const RcString& name, ResolveNamespace ns, ::AST::AbsolutePath* out_path=nullptr)
    -> ResolveItemRef {
    if (mod.indexPopulated) {
        TODO(sp, "Look up in index");
    }

    // Prevent infinite recursion
    // - Includes the target name to only catch on nested lookups of the same name
    auto guardEnt = ::std::make_pair(&mod, name);
    bool visitUse = true;
    if (std::count(antirecurseStack.begin(), antirecurseStack.end(), guardEnt) > 0) {
        visitUse = false;
    }

    antirecurseStack.push_back(std::move(guardEnt));
    STD_DEFER {
        antirecurseStack.pop_back();
    };

    if (ns == ResolveNamespace::Macro) {
        for (const auto& i : mod.macros()) {
            if (i.name == name) {
                if (outPath) {
                    outPath->nodes.push_back(name);
                }
                return ResolveItemRef::make_Macro(&*i.data);
            }
        }
        for (const auto& mac : reverse(mod.macroImports)) {
            if (mac.ref.is_None()) {
                // Skip
                continue;
            }
            if (mac.name == name) {
                // TODO: What about macro re-exports a builtin?
                if (outPath) {
                    *outPath = mac.path;
                }
                switch (mac.ref.tag()) {
                    case MacroRef::TAG_None: {
                        BUG(sp, "macro_imports_res had a None entry");
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
        // Note: Cache the result of `cfg()` resolution, as it doesn't change
        // - Do the caching here (on the item level) instead of in `cfg.cpp` as that avoids needing to check
        //   the attribute list multiple times.
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
            switch (ns) {
                case ResolveNamespace::Macro:
                    if (const auto* mac = i->data.opt_Macro()) {
                        if (i->attrs.get("rustc_builtin_macro")) {
                            auto* rv = ExpandFindProcMacro(crate.wb, name);
                            if (rv) {
                                return ResolveItemRefMacro(rv);
                            }
                            // HACK: Ignore, as there's references to the `Debug` macro... but trustme doesn't do things that way
                            // - Probably should have derives be in the same namespace as macros
                        }
                        return ResolveItemRefMacro(&**mac);
                    }
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
                                    TODO(sp, "ImplicitPrelude?");
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

                    switch (tgtMod.tag()) {
                        case ResolveModuleRef::TAG_Ast: {
                            auto& modPtr = tgtMod.as_Ast();
                            // NOTE: Recursion
                            auto rv = this->findItem(*modPtr, itemName, ns, outPath);
                            if (!rv.is_None()) {
                                return rv;
                            }
                            break;
                        }
                        case ResolveModuleRef::TAG_Hir: {
                            auto& modPtr = tgtMod.as_Hir();
                            // If `get_module` provided a HIR module, then this is right?
                            // - What if it's an alias? (not critical)
                            auto rv = this->findItemHir(*modPtr, itemName, ns, outPath);
                            if (!rv.is_None()) {
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
                                TODO(sp, "ImplicitPrelude?");
                            }
                            break;
                        }
                        case ResolveModuleRef::TAG_None: {
                            // Ignore for now?
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
                    // - Outer recurse
                    //  > Get the module for this path
                    auto srcMod = this->getModule(mod.path(), e.path, /*ignore_last=*/false, outPath);
                    switch (srcMod.tag()) {
                        case ResolveModuleRef::TAG_None: {
                            auto& _ = srcMod.as_None();
                            break;
                        }
                        case ResolveModuleRef::TAG_ImplicitPrelude: {
                            TODO(sp, "ImplicitPrelude? " << e.path);
                            break;
                        }
                        case ResolveModuleRef::TAG_Ast: {
                            auto& sm = srcMod.as_Ast();
                            auto rv = findItem(*sm, name, ns, outPath);
                            if (!rv.is_None()) {
                                return rv;
                            }
                            // Fall through, keep searching
                            break;
                        }
                        case ResolveModuleRef::TAG_Hir: {
                            auto& sm = srcMod.as_Hir();
                            auto rv = this->findItemHir(*sm, name, ns, outPath);
                            if (!rv.is_None()) {
                                return rv;
                            }
                            // Not found, fall through
                            break;
                        }
                    }
                }
            }
        }
    }
    if (mod.isAnon()) {
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
    return ResolveItemRef::make_None({});
}

auto ResolveState::findItemHir(const HIRModule& mod, const RcString& itemName, ResolveNamespace ns, ASTAbsolutePath* outPath, const HIRSimplePath* visPathP) -> ResolveItemRef {
    const auto& visPath = visPathP ? *visPathP : HIRSimplePath();
    if (outPath) {
        ASSERT_BUG(sp, outPath->crate != "", "Crate not filled");
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

    // Note, `out_path` should be populated to this module's path
    switch (ns) {
        case ResolveNamespace::Namespace: {
            auto it = mod.modItems.find(itemName);
            if (it != mod.modItems.end() && it->second->publicity.isVisible(visPath)) {
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
                ASSERT_BUG(sp, !ti->is_Import(), "Recursive namespace import in HIR: " << it->second->ent.as_Import().path << " pointed to " << ti->as_Import().path);
                return ResolveItemRefType(ti);
            }
        } break;
        case ResolveNamespace::Value: {
            auto it = mod.valueItems.find(itemName);
            if (it != mod.valueItems.end() && it->second->publicity.isVisible(visPath)) {
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
                ASSERT_BUG(sp, !vi->is_Import(), "Recursive value import in HIR: " << it->second->ent.as_Import().path << " pointed to " << vi->as_Import().path);
                return ResolveItemRefValue(vi);
            }
        } break;
        case ResolveNamespace::Macro: {
            auto it = mod.macroItems.find(itemName);
            if (it == mod.macroItems.end()) {
            } else if (!it->second->publicity.isVisible(visPath)) {
            } else {
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
                            //    TODO(sp, "Resolve HIR import to decorator");
                            //    //return ResolveItemRef_Macro(pm);
                            //}
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
                        // Fall throught to fail
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
                        BUG(sp, "Recursive macro import in HIR: " << it->second->ent.as_Import().path << " pointed to " << me.path);
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
