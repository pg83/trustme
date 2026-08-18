#include "resolve_common.h"

#include "ast_ast.h"
#include "hir_hir.h"
#include <span> // std::span
#include "ast_expr.h"
#include "settings.h"
#include "ast_crate.h"
#include "expand_cfg.h"
#include "synext_macro.h"
#include "resolve_main_bindings.h"

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

        ResolveState(const Span& span, const Settings& settings, const ASTCrate& crate)
            : sp(span)
            , settings(settings)
            , crate(crate)
        {
        }

        /// <summary>
        /// Obtain a reference to the specified module
        /// </summary>
        ResolveModuleRef getModule(const ASTPath& basePath, const ASTPath& path, bool ignoreLast, ASTAbsolutePath* outPath, bool ignoreHygiene = false) {
            TRACE_FUNCTION_F(path << " in " << basePath << (ignoreLast ? " (ignore last)" : ""));
            const auto& baseNodes = basePath.nodes();
            TU_MATCH_HDRA( (path.cls), {)
            TU_ARMA(Invalid, e) {
                    // Should never happen
                    BUG(sp, "Invalid path class encountered");
                }
                TU_ARMA(Local, e) {
                    // Wait, how is this already known?
                    BUG(sp, "Local path class in use statement");
                }
                TU_ARMA(UFCS, e) {
                    // Wait, how is this already known?
                    BUG(sp, "UFCS path class in use statement");
                }

                TU_ARMA(Relative, e) {
                    DEBUG("Relative " << path);
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
                            DEBUG("Trying implicit externs for " << name);
                            DEBUG(FmtLambda([&](std::ostream& os) {
                                for (const auto& v : settings.implicitCrates) {
                                    os << " " << v.first;
                                }
                            }));
                            auto ecIt = settings.implicitCrates.find(name);
                            if (ecIt != settings.implicitCrates.end()) {
                                return ResolveModuleRef::make_ImplicitPrelude({});
                            }
                        }

                        DEBUG("Ignore last");
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
                    TU_MATCH_HDRA( (realMod), {)
                    TU_ARMA(Ast, iData) {
                                // TODO: What about an enum?
                        TU_MATCH_HDRA( (*iData), {)
                        default: {
                                        // Ignore, keep going
                                    }
                                    TU_ARMA(Crate, c) {
                                        if (outPath) {
                                            *outPath = ASTAbsolutePath(c.name, {});
                                        }
                                        if (c.name == "") {
                                            return getModuleAst(crate.rootModule_, path, 1, ignoreLast, outPath);
                                        }
                                        ASSERT_BUG(sp, crate.externCrates.count(c.name) > 0, "Unable to find crate `" << c.name << "`");
                                        return getModuleHir(crate.externCrates.at(c.name).hir->rootModule, path, 1, ignoreLast, outPath);
                                    }
                                    TU_ARMA(Module, m) {
                                        return getModuleAst(m, path, 1, ignoreLast, outPath);
                                    }
                        }
                            }
                            TU_ARMA(AstRoot, m) {
                                if (outPath) {
                                    *outPath = ASTAbsolutePath("", {});
                                }
                                return getModuleAst(*m, path, 1, ignoreLast, outPath);
                            }
                            TU_ARMA(HirRoot, hirCrate) {
                                return getModuleHir(hirCrate->rootModule, path, 1, ignoreLast, outPath);
                            }
                            TU_ARMA(Hir, iEntPtr) {
                                ASSERT_BUG(sp, !iEntPtr->is_Import(), "");
                                if (iEntPtr->is_Enum()) {
                                    DEBUG("Enum");
                                    return ResolveModuleRef();
                                }

                                //}
                                ASSERT_BUG(sp, iEntPtr->is_Module(), "Expected Module, got " << iEntPtr->tagStr() << " for " << name << " in [" << baseNodes << "]");
                                return getModuleHir(iEntPtr->as_Module(), path, 1, ignoreLast, outPath);
                                //}
                            }
                            TU_ARMA(None, e) {
                                // Not found in this module, keep searching
                                DEBUG("Keep searching (" << i << "/" << baseNodes.size() << ")");
                            }
                    }

                    i += 1;
                    } while (i < baseNodes.size() && baseNodes[baseNodes.size() - i].name().c_str()[0] == '#');

                    // If not found, look for an implicit crate allowed in this edition.
                    if (crate.edition >= ASTEdition::Rust2018 || name == "core" || name == "std") {
                        DEBUG("Trying implicit externs for " << name);
                        DEBUG(FmtLambda([&](std::ostream& os) {
                            for (const auto& v : settings.implicitCrates) {
                                os << " " << v.first;
                            }
                        }));
                        auto ecIt = settings.implicitCrates.find(name);
                        if (ecIt != settings.implicitCrates.end()) {
                            if (ecIt->second == "") {
                                // This crate!
                                return getModuleAst(crate.rootModule_, path, 1, ignoreLast, outPath);
                            } else {
                                ASSERT_BUG(sp, crate.externCrates.count(ecIt->second), "Crate \"" << ecIt->second << "\" not loaded (for \"" << ecIt->first << "\")");
                                const auto& ec = crate.externCrates.at(ecIt->second);
                                DEBUG("Implicitly imported crate");
                                if (outPath) {
                                    *outPath = ASTAbsolutePath(ecIt->second, {});
                                }
                                return getModuleHir(ec.hir->rootModule, path, 1, ignoreLast, outPath);
                            }
                        }
                    }
                    DEBUG("Not found");
                    return ResolveModuleRef();
                }

                // Simple logic
                TU_ARMA(Self, e) {
                    DEBUG("Self " << path);
                    // Look up within the non-anon module
                    size_t i = 0;
                    while (i < baseNodes.size() && baseNodes[baseNodes.size() - i - 1].name().c_str()[0] == '#') {
                        i += 1;
                    }
                    const auto& startMod = this->getModByTruePath(baseNodes, baseNodes.size() - i);
                    return getModuleAst(startMod, path, 0, ignoreLast, outPath);
                }
                TU_ARMA(Super, e) {
                    DEBUG("Super " << path);
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
                TU_ARMA(Absolute, e) {
                    DEBUG("Absolute " << path);
                    if (e.crate == "" || e.crate == crate.crateNameReal) {
                        DEBUG("Current crate");
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
                        auto ecIt2 = crate.externCrates.find(ecIt->second);
                        if (ecIt2 == crate.externCrates.end()) {
                            DEBUG("Crate " << ecIt->second << " not found");
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
                            DEBUG("Crate " << e.crate << " not found");
                            return ResolveModuleRef();
                        }
                        if (outPath) {
                            *outPath = ASTAbsolutePath(e.crate, {});
                        }
                        return getModuleHir(ecIt->second.hir->rootModule, path, 0, ignoreLast, outPath);
                    }
                }
            }
            throw "";
        }

        ResolveModuleRef getModuleAst(const ASTModule& startMod, const ASTPath& path, size_t startOffset, bool ignoreLast, ASTAbsolutePath* outPath) {
            TRACE_FUNCTION_F("start_offset=" << startOffset << ", ignore_last=" << ignoreLast);
            const ASTModule* mod = &startMod;
            ASSERT_BUG(Span(), path.nodes().size() >= (ignoreLast ? 1 : 0), "" << path);

            for (size_t idx = startOffset; idx < path.nodes().size() - (ignoreLast ? 1 : 0); idx++) {
                const auto& name = path.nodes()[idx].name();

                auto res = findItem(*mod, name, ResolveNamespace::Namespace, outPath);
                if (res.is_None()) {
                    DEBUG("Unable to find " << name << " in module " << mod->path() << " for " << path);
                    return ResolveModuleRef();
                }
                const auto& r = res.as_Namespace();
                TU_MATCH_HDRA( (r), { )
                TU_ARMA(None, e) {
                        DEBUG("Not found (Namespace::None)");
                        return ResolveModuleRef();
                    }
                    TU_ARMA(Ast, e) {
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
                            DEBUG("Found " << e->tagStr() << ", not module");
                            return ResolveModuleRef();
                        }
                    }
                    TU_ARMA(AstRoot, e) {
                        mod = e;
                    }
                    TU_ARMA(Hir, e) {
                        if (const auto* i = e->opt_Module()) {
                            return getModuleHir(*i, path, idx + 1, ignoreLast, outPath);
                        } else {
                            DEBUG("Found HIR " << e->tagStr() << ", not module");
                            return ResolveModuleRef();
                        }
                    }
                    TU_ARMA(HirRoot, e) {
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

        ResolveModuleRef getModuleHir(const HIRModule& startMod, const ASTPath& path, size_t startOffset, bool ignoreLast, ASTAbsolutePath* outPath) {
            TRACE_FUNCTION_F("path=" << path << ", start_offset=" << startOffset << ", ignore_last=" << ignoreLast);
            const HIRModule* mod = &startMod;
            ASSERT_BUG(Span(), path.nodes().size() >= (ignoreLast ? 1 : 0), "" << path);
            for (size_t i = startOffset; i < path.nodes().size() - (ignoreLast ? 1 : 0); i++) {
                const auto& name = path.nodes()[i].name();
                // Find the module for this node
                auto it = mod->modItems.find(name);
                if (it == mod->modItems.end() || !it->second->publicity.isGlobal()) {
                    DEBUG(name << " Not Found");
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
                TU_MATCH_HDRA( (*ti), {)
                default:
                    DEBUG(name << " Not Module, instead " << ti->tagStr());
                    return ResolveModuleRef();
                    TU_ARMA(Module, m) {
                        mod = &m;
                    }
                }
            }
            if (outPath) {
                ASSERT_BUG(sp, outPath->crate != "", "Invalid HIR output path - crate name not set");
            }
            return ResolveModuleRef(mod);
        }

        const ASTModule& getModByTruePath(const std::vector<ASTPathNode>& baseNodes, size_t len) {
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

        static bool matchingNamespace(const ASTItem& i, ResolveNamespace ns) {
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
            throw "";
        }

        ResolveItemRef findItem(const ASTModule& mod, const RcString& name, ResolveNamespace ns, ASTAbsolutePath* outPath = nullptr)
        //ResolveModuleRef get_source_module_for_name(const AST::Module& mod, const RcString& name, ResolveNamespace ns, ::AST::AbsolutePath* out_path=nullptr)
        {
            TRACE_FUNCTION_F("Looking for " << name << " in " << mod.path() << " (ns=" << ns << ")");
            if (mod.indexPopulated) {
                TODO(sp, "Look up in index");
            }

            // Prevent infinite recursion
            // - Includes the target name to only catch on nested lookups of the same name
            auto guardEnt = ::std::make_pair(&mod, name);
            bool visitUse = true;
            if (std::count(antirecurseStack.begin(), antirecurseStack.end(), guardEnt) > 0) {
                DEBUG("Recursion detected, not looking at `use` statements in " << mod.path());
                visitUse = false;
            }

            struct Guard {
                std::vector<antirecurseStackEntT>& s;

                Guard(std::vector<antirecurseStackEntT>& s, antirecurseStackEntT e)
                    : s(s)
                {
                    s.push_back(std::move(e));
                }

                ~Guard() {
                    s.pop_back();
                }
            } guard(antirecurseStack, guardEnt);

            if (ns == ResolveNamespace::Macro) {
                for (const auto& i : mod.macros()) {
                    DEBUG("> MACRO " << i.name);
                    if (i.name == name) {
                        DEBUG("Found in ast (macro)");
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
                        DEBUG("Found in ast (macro import) - " << mac.path);
                        if (outPath) {
                            *outPath = mac.path;
                        }
                        TU_MATCH_HDRA( (mac.ref), { )
                        TU_ARMA(None, me) {
                                BUG(sp, "macro_imports_res had a None entry");
                            }
                            TU_ARMA(MacroRules, me)
                            return ResolveItemRefMacro(me);
                            TU_ARMA(BuiltinProcMacro, me)
                            return ResolveItemRefMacro(me);
                            TU_ARMA(ExternalProcMacro, me)
                            return ResolveItemRefMacro(me);
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
                    DEBUG("Found in ast (" << i->data.tagStr() << ")");
                    switch (ns) {
                        case ResolveNamespace::Macro:
                            if (const auto* mac = i->data.opt_Macro()) {
                                if (i->attrs.get("rustc_builtin_macro")) {
                                    auto* rv = ExpandFindProcMacro(name);
                                    if (rv) {
                                        return ResolveItemRefMacro(rv);
                                    }
                                    // HACK: Ignore, as there's references to the `Debug` macro... but trustme doesn't do things that way
                                    // - Probably should have derives be in the same namespace as macros
                                }
                                return ResolveItemRefMacro(&**mac);
                            }
                            DEBUG("- Ignoring macro");
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
                            DEBUG("Use " << e.name << " := " << e.path);

                            if (e.path.cls.is_Absolute() && e.path.cls.as_Absolute().crate == CRATE_BUILTINS) {
                                const auto& pe = e.path.cls.as_Absolute();
                                if (ns == ResolveNamespace::Macro) {
                                    if (outPath) {
                                        outPath->crate = pe.crate;
                                        outPath->nodes = makeVec1<RcString>(RcString(pe.nodes.front().name()));
                                    }
                                    return ResolveItemRefMacro(ExpandFindProcMacro(pe.nodes.front().name()));
                                }
                            }
                            if (e.path.cls.is_Absolute() && e.path.cls.as_Absolute().nodes.empty()) {
                                if (ns == ResolveNamespace::Namespace) {
                                    ASTAbsolutePath tmp;
                                    auto tgtMod = this->getModule(mod.path(), e.path, false, &tmp);
                                    TU_MATCH_HDRA( (tgtMod), {)
                                    TU_ARMA(Ast, modPtr) {
                                            if (outPath) {
                                                *outPath = tmp;
                                            }
                                            return ResolveItemRefType::make_AstRoot(modPtr);
                                        }
                                        TU_ARMA(Hir, modPtr) {
                                            if (outPath) {
                                                *outPath = tmp;
                                            }
                                            return ResolveItemRefType::make_HirRoot(&*crate.externCrates.at(tmp.crate).hir);
                                        }
                                        TU_ARMA(ImplicitPrelude, _e) {
                                            TODO(sp, "ImplicitPrelude?");
                                        }
                                        TU_ARMA(None, _e) {
                                        }
                                    }
                                }
                                continue;
                            }

                            const auto& itemName = e.path.nodes().back().name();
                            auto tgtMod = this->getModule(mod.path(), e.path, true, outPath);
                            DEBUG(tgtMod.tagStr());

                            TU_MATCH_HDRA( (tgtMod), {)
                            TU_ARMA(Ast, modPtr) {
                                    // NOTE: Recursion
                                    auto rv = this->findItem(*modPtr, itemName, ns, outPath);
                                    if (!rv.is_None()) {
                                        DEBUG("Found in AST use");
                                        return rv;
                                    }
                                }
                                TU_ARMA(Hir, modPtr) {
                                    // If `get_module` provided a HIR module, then this is right?
                                    // - What if it's an alias? (not critical)
                                    auto rv = this->findItemHir(*modPtr, itemName, ns, outPath);
                                    if (!rv.is_None()) {
                                        DEBUG("Found in HIR use");
                                        return rv;
                                    }
                                }
                                TU_ARMA(ImplicitPrelude, _e) {
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
                                }
                                TU_ARMA(None, _e) {
                                    // Ignore for now?
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
                            DEBUG("Glob use " << e.path);

                            // - Outer recurse
                            //  > Get the module for this path
                            auto srcMod = this->getModule(mod.path(), e.path, /*ignore_last=*/false, outPath);
                            TU_MATCH_HDRA( (srcMod), {)
                            TU_ARMA(None, _) {
                                    DEBUG("Unable to find " << e.path);
                                    continue;
                                }
                                TU_ARMA(ImplicitPrelude, _e) {
                                    TODO(sp, "ImplicitPrelude? " << e.path);
                                }
                                TU_ARMA(Ast, sm) {
                                    auto rv = findItem(*sm, name, ns, outPath);
                                    if (!rv.is_None()) {
                                        DEBUG("Found in AST glob");
                                        return rv;
                                    }
                                    // Fall through, keep searching
                                }
                                TU_ARMA(Hir, sm) {
                                    auto rv = this->findItemHir(*sm, name, ns, outPath);
                                    if (!rv.is_None()) {
                                        DEBUG("Found HIR glob");
                                        return rv;
                                    }
                                    // Not found, fall through
                                }
                            }
                        }
                    }
                }
            }
            if (mod.isAnon()) {
                DEBUG("Recurse to parent");
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
            DEBUG("Not found");
            return ResolveItemRef::make_None({});
        }

        /// Locate the named item in HIR (resolving `Import` references too)
        ResolveItemRef findItemHir(const HIRModule& mod, const RcString& itemName, ResolveNamespace ns, ASTAbsolutePath* outPath = nullptr, const HIRSimplePath* visPathP = nullptr) {
            const auto& visPath = visPathP ? *visPathP : HIRSimplePath();
            TRACE_FUNCTION_F(itemName);
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
                        DEBUG("Found `" << itemName << "` in HIR namespace");
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
                        DEBUG("Found `" << itemName << "` in HIR value");
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
                        DEBUG("Did not find `" << itemName << "` in HIR macro");
                    } else if (!it->second->publicity.isVisible(visPath)) {
                        DEBUG("Found `" << itemName << "` in HIR macro - but not public, ignoring");
                    } else {
                        DEBUG("Found `" << itemName << "` in HIR macro");
                        const HIRMacroItem* mi;
                        if (const auto* p = it->second->ent.opt_Import()) {
                            if (outPath) {
                                *outPath = spToAp(p->path);
                            }

                            struct H2 {
                                static ResolveItemRefMacro getBuiltin(const Span& sp, const RcString& name) {
                                    // TODO: What if it's a derive? Or it's an attribute
                                    if (auto* pm = ExpandFindProcMacro(name)) {
                                        return ResolveItemRefMacro(pm);
                                    }
                                    //    TODO(sp, "Resolve HIR import to decorator");
                                    //    //return ResolveItemRef_Macro(pm);
                                    //}
                                    DEBUG("Import of builtins: Not found");
                                    return {};
                                }
                            };

                            if (p->path.crateName() == CRATE_BUILTINS) {
                                auto v = H2::getBuiltin(sp, p->path.components().back());
                                if (v.is_None()) {
                                    break;
                                }
                                return v;
                            }
                            mi = &H::getCrate(sp, crate, p->path).getMacroitemByPath(sp, p->path, true);
                            if (const auto* p = mi->opt_Import()) {
                                if (p->path.crateName() == CRATE_BUILTINS) {
                                    auto v = H2::getBuiltin(sp, p->path.components().back());
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
                    TU_MATCH_HDRA( (*mi), {)
                    TU_ARMA(Import, me) {
                                BUG(sp, "Recursive macro import in HIR: " << it->second->ent.as_Import().path << " pointed to " << me.path);
                            }
                            TU_ARMA(MacroRules, me) {
                                return ResolveItemRefMacro(&*me);
                            }
                            TU_ARMA(ProcMacro, me) {
                                return ResolveItemRefMacro(&me);
                            }
                    }
                    }
                } break;
            }

            return ResolveItemRef::make_None({});
        }
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
    TRACE_FUNCTION_F("path=" << path << " in " << basePath);
    ResolveState rs(span, settings, crate);

    const auto& itemName = path.nodes().back().name();
    auto mod = rs.getModule(basePath, path, true, outPath);
    if (mod.is_ImplicitPrelude()) {
        const auto& baseNodes = basePath.nodes();
        mod = ResolveModuleRef(&rs.getModByTruePath(baseNodes, baseNodes.size()));
    }
    TU_MATCH_HDRA( (mod), {)
    TU_ARMA(Ast, modPtr) {
            auto rv = rs.findItem(*modPtr, itemName, ResolveNamespace::Macro, outPath);
            if (rv.is_None()) {
                return ResolveItemRefMacro::make_None({});
            }
            ASSERT_BUG(span, rv.is_Macro(), rv.tagStr());
            return std::move(rv.as_Macro());
        }
        TU_ARMA(Hir, modPtr) {
            const HIRSimplePath* visPath = nullptr;
            HIRSimplePath tmpP;
            if (path.cls.is_Relative() && path.cls.as_Relative().hygiene.hasModPath()) {
                const auto& inP = path.cls.as_Relative().hygiene.modPath();
                tmpP = HIRSimplePath(inP.crate, inP.ents);
                DEBUG("vis_path=" << tmpP);
                visPath = &tmpP;
            }
            auto rv = rs.findItemHir(*modPtr, itemName, ResolveNamespace::Macro, outPath, visPath);
            if (rv.is_None()) {
                return ResolveItemRefMacro::make_None({});
            }
            ASSERT_BUG(span, rv.is_Macro(), rv.tagStr());
            return std::move(rv.as_Macro());
        }
        TU_ARMA(ImplicitPrelude, _e) {
            // This isn't a macro, so return `None`
            return ResolveItemRefMacro::make_None({});
        }
        TU_ARMA(None, e) {
            return ResolveItemRefMacro::make_None({});
        }
    }
    // Technically a bug to reach this point.
    return ResolveItemRefMacro::make_None({});
}

/// Returns the source module for the specified name
// NOTE: Name resolution
ResolveModuleRef ResolveLookupGetModuleForName(const Span& sp, const Settings& settings, const ASTCrate& crate, const ASTPath& basePath, const ASTPath& path, ResolveNamespace ns, ASTAbsolutePath* outPath) {
    TRACE_FUNCTION_F("path=" << path << " in " << basePath);
    ResolveState rs(sp, settings, crate);

    auto mod = rs.getModule(basePath, path, true, outPath);
    TU_MATCH_HDRA( (mod), {)
    TU_ARMA(Ast, modPtr) {
            ASTAbsolutePath tmp;
            if (!outPath) {
                outPath = &tmp;
            }
            auto res = rs.findItem(*modPtr, path.nodes().back().name(), ns, outPath);
            if (res.is_None()) {
                BUG(sp, "Unable to find " << path << " (starting from " << basePath << ")");
            }

            TODO(sp, "");
        }
        TU_ARMA(Hir, modPtr) {
            // If `get_module` provided a HIR module, then this is right?
            // - What if it's an alias? (not critical)
            return mod;
        }
        TU_ARMA(ImplicitPrelude, _e) {
            return mod;
        }
        TU_ARMA(None, e) {
            BUG(sp, "Unable to find " << path << " (starting from " << basePath << ")");
        }
    }
    throw "";
}
