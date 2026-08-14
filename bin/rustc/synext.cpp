#include "synext.h"
#include "wire_board.h"
#include "synext.h"

#include "ast_ast.h"
#include "hir_hir.h" // for HIR::Crate
#include "ast_expr.h"
#include "ast_crate.h"
#include "parse_common.h"
#include "parse_ttstream.h"
#include "macro_rules_macro_rules.h"

class CMacroRulesExpander: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override {
        ERROR(sp, E0000, "macro_rules! requires an identifier");
    }

    ::std::unique_ptr<TokenStream> expandIdent(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const RcString& ident, const TokenTree& tt, ASTModule& mod) override {
        DEBUG("Parsing macro_rules! " << ident);
        TTStream lex(sp, ParseState(), tt);
        auto mac = ParseMacroRules(lex);
        mac->definitionSpan = sp;
        DEBUG("macro_rules! " << mod.path() + ident << " " << &*mac);
        mod.addMacro(false, ident, mv$(mac));

        return ::std::unique_ptr<TokenStream>(new TTStreamO(sp, ParseState(), TokenTree()));
    }
};

class CMacroUseHandler: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    bool runDuringIter() const override {
        return true;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        TRACE_FUNCTION_F("[CMacroUseHandler] path=" << path);

        std::vector<RcString> filter;
        if (mi.data().size() > 0) {
            mi.parseParenIdentList([&](const Span& sp, RcString ident) {
                filter.push_back(ident);
            });
        }
        std::vector<bool> filtersUsed(filter.size());

        auto filterValid = [&](RcString name) -> bool {
            if (filter.empty()) {
                return true;
            }
            auto it = std::find(filter.begin(), filter.end(), name);
            if (it != filter.end()) {
                auto i = it - filter.begin();
                filtersUsed[i] = true;
                return true;
            } else {
                return false;
            }
        };

        auto exists = [&mod](const RcString& name, const MacroRef& mr) -> bool {
            for (const auto& imp : mod.macroImports) {
                if (imp.name != name) {
                    continue;
                }
                if (imp.ref.tag() != mr.tag()) {
                    continue;
                }
                bool rv;
                TU_MATCH_HDRA( (imp.ref, mr), {)
                TU_ARMA(None, a,b) {
                        rv = true;
                    }
                    TU_ARMA(MacroRules, a, b) {
                        rv = (a == b);
                    }
                    TU_ARMA(BuiltinProcMacro, a, b) {
                        rv = (a == b);
                    }
                    TU_ARMA(ExternalProcMacro, a, b) {
                        rv = (a == b);
                    }
                }
                if(rv) {
                    return true;
                }
            }
            return false;
        };

        if (i.is_None()) {
            // Just ignore
        } else if (const auto* ecItem = i.opt_Crate()) {
            const auto& ec = crate.externCrates.at(ecItem->name);

            DEBUG(ec.hir->exportedMacroNames.size() << " exported macros");
            for (const auto& name : ec.hir->exportedMacroNames) {
                if (!filterValid(name)) {
                    DEBUG("Skip " << name);
                    continue;
                }
                ASSERT_BUG(sp, ec.hir->mRootModule.macroItems.count(name) == 1, "Macro `" << name << "` missing from crate " << ec.mName);
                const auto* e = &*ec.hir->mRootModule.macroItems.at(name);
                if (!e->publicity.isGlobal()) {
                    DEBUG("Not public: " << name);
                    continue;
                }

                ASTAbsolutePath path{ecItem->name, {name}};
                if (const auto* imp = e->ent.opt_Import()) {
                    if (imp->path.crateName() == CRATE_BUILTINS) {
                        DEBUG("Importing builtin (skip): " << name);
                        continue;
                    }
                    ASSERT_BUG(sp, crate.externCrates.count(imp->path.crateName()), "Crate `" << imp->path.crateName() << "` not loaded");
                    const HIRModule& mod = crate.externCrates.at(imp->path.crateName()).hir->getModByPath(sp, imp->path, /*ignore_last_node*/ true, /*ignore_crate_name*/ true);

                    ASSERT_BUG(sp, mod.macroItems.count(imp->path.components().back()), "Failed to find final component of " << imp->path);
                    e = &*mod.macroItems.at(imp->path.components().back());
                    if (const auto& imp2 = e->ent.opt_Import()) {
                        if (imp2->path.crateName() == CRATE_BUILTINS) {
                            DEBUG("Importing builtin (skip): " << name);
                            continue;
                        } else {
                            ASSERT_BUG(sp, !e->ent.is_Import(), "Recursive import - " << imp->path << " pointed to " << imp2->path);
                        }
                    } else {
                    }
                    path = ASTAbsolutePath(imp->path.crateName(), imp->path.componentsVec());
                }

                MacroRef mr;
                TU_MATCH_HDRA( (e->ent), { )
                TU_ARMA(Import, imp) {
                        throw "Unexpected";
                    }
                    TU_ARMA(MacroRules, macPtr) {
                        mr = &*macPtr;
                    }
                    TU_ARMA(ProcMacro, p) {
                        mr = &p;
                    }
                }
                if(!exists(name, mr))
                {
                    auto mi = ASTModule::MacroImport{false, name, std::move(path), std::move(mr)};
                    DEBUG("Import macro " << mi.path);
                    mod.macroImports.push_back(mv$(mi));
                }
            }
        } else if (const auto* submodP = i.opt_Module()) {
            const auto& submod = *submodP;
            for (const auto& mr : submod.macros()) {
                if (!filterValid(mr.name)) {
                    continue;
                }
                DEBUG("Imported " << mr.name);
                if (!exists(mr.name, &*mr.data)) {
                    auto path = submod.path();
                    path.nodes.push_back(mr.name);
                    DEBUG(mod.path() << ": Import macro " << path);
                    mod.macroImports.push_back(ASTModule::MacroImport{false, mr.name, path, &*mr.data});
                }
            }
            for (const auto& mri : submod.macroImports) {
                if (!filterValid(mri.name)) {
                    continue;
                }
                DEBUG(mod.path() << ": Imported " << mri.name << " (propagate) = " << mri.path);
                if (!exists(mri.name, mri.ref)) {
                    mod.macroImports.push_back(mri.clone());
                }
            }
        } else {
            WARNING(sp, W0000, "Use of #[macro_use] on non-module/crate - " << i.tagStr());
            return;
        }

        for (size_t i = 0; i < filter.size(); i++) {
            if (!filtersUsed[i]) {
                ERROR(sp, E0000, "Couldn't find macro " << filter[i]);
            }
        }
    }
};

class CMacroExportHandler: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        // TODO: Flags on the attribute
        // - `local_inner_macros`: Forces macro lookups within the expansion to search within the source crate
        //   > Strictly speaking, not the same as `macro`-style macros?
        bool localInnerMacros = false;
        if (mi.data().size() > 0) {
            mi.parseParenIdentList([&](const Span& sp, RcString ident) {
                if (ident == "local_inner_macros") {
                    localInnerMacros = true;
                } else {
                    ERROR(sp, E0000, "Unknown option for #[macro_export] - " << ident);
                }
            });
        }

        if (i.is_None()) {
        }
        // If on a `use` it's for a #[rustc_builtin_macro]
        else if (const auto* u = i.opt_Use()) {
            if (u->entries.size() == 1 && u->entries.back().path.isAbsolute() && u->entries.back().path.cls.as_Absolute().crate == CRATE_BUILTINS && u->entries.back().path.cls.as_Absolute().nodes.size() == 1)
                ;
            else {
                ERROR(sp, E0000, "Use of #[macro_export] on non-macro - " << i.tagStr());
            }
            const auto& p = u->entries.back().path.cls.as_Absolute();
            const auto& name = p.nodes.front().name();
            mod.macroImports.push_back(ASTModule::MacroImport{true, u->entries.front().name, ASTAbsolutePath(p.crate, {name}), {}});

            crate.mRootModule.addItem(sp, ASTVisibility::makeGlobal(), name, i.clone(), {});
        } else if (i.is_MacroInv()) {
            const auto& mac = i.as_MacroInv();
            if (!(mac.path().isTrivial() && mac.path().asTrivial() == "macro_rules")) {
                ERROR(sp, E0000, "#[macro_export] is only valid on macro_rules!");
            }
            const auto& name = mac.inputIdent();

            // Tag the macro in the module for crate export
            // AND move it to the root module
            auto it = ::std::find_if(mod.macros().begin(), mod.macros().end(), [&](const auto& x) {
                return x.name == name;
            });
            ASSERT_BUG(sp, it != mod.macros().end(), "Macro '" << name << "' not defined in this module");
            auto e = mv$(*it);
            mod.macros().erase(it);

            // Leave an alias here, so existing references are valid
            mod.macroImports.push_back(ASTModule::MacroImport{false, name, ASTAbsolutePath("", {name}), &*e.data});
            DEBUG(mod.path() << ": macro_use Import " << mod.macroImports.back().name << " = " << mod.macroImports.back().path);

            if (localInnerMacros) {
                Ident::ModPath mp;
                mp.crate = "";
                // Empty node list, will search the crate root
                // TODO: Strictly speaking, this shouldn't apply to non-macro paths
                DEBUG("#[macro_export(local_inner_macros)] mp=" << mp);
                e.data->mHygiene.setModPath(*wb.pool, mv$(mp));
            }

            e.data->exported = true;
            DEBUG("- Export macro " << name << "!");
            crate.mRootModule.macros().push_back(mv$(e));
        } else if (i.is_Macro()) {
            const auto& name = path.nodes.back();
            if (i.as_Macro()) {
                i.as_Macro()->exported = true;
                ASSERT_BUG(sp, path.nodes.size() == 1, "");
                DEBUG("- Export macro (item) " << name << "!");
            }
        } else {
            ERROR(sp, E0000, "Use of #[macro_export] on non-macro - " << i.tagStr());
        }
    }
};

class CMacroReexportHandler: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        if (!i.is_Crate()) {
            ERROR(sp, E0000, "Use of #[macro_reexport] on non-crate - " << i.tagStr());
        }

        const auto& crateName = i.as_Crate().name;
        auto& extCrate = *crate.externCrates.at(crateName).hir;

        mi.parseParenIdentList([&](const Span& sp, RcString name) {
            auto it = ::std::find(extCrate.exportedMacroNames.begin(), extCrate.exportedMacroNames.end(), name);
            if (it == extCrate.exportedMacroNames.end()) {
                ERROR(sp, E0000, "Could not find macro " << name << "! in crate " << crateName);
            }
            // TODO: Do this differently.
            extCrate.mRootModule.macroItems.at(name)->ent.as_MacroRules()->exported = true;
        });
    }
};

class CBuiltinMacroHandler: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& /*mod*/, size_t /*mod_idx*/, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        RcString name;
        if (i.is_MacroInv()) {
            const auto& e = i.as_MacroInv();
            if (!(e.path().isTrivial() && e.path().asTrivial() == "macro_rules")) {
                ERROR(sp, E0000, "Use of #[rustc_builtin_macro] on macro other than macro_rules! - " << i.tagStr());
            }
            name = e.inputIdent();
        } else if (i.is_Macro()) {
            name = path.nodes.back();
        } else {
            ERROR(sp, E0000, "Use of #[rustc_builtin_macro] on non-macro - " << i.tagStr());
        }

        ASTUseItem ui;
        ui.entries.push_back(ASTUseItem::Ent{});
        ui.entries.back().name = name;
        ui.entries.back().path = ASTPath(RcString::newInterned(CRATE_BUILTINS), {name});
        DEBUG("Convert macro_rules tagged #[rustc_builtin_macro] with use - " << name);
        i = ASTItem::make_Use(mv$(ui));
    }
};

STATIC_MACRO("macro_rules", CMacroRulesExpander);
STATIC_DECORATOR("macro_use", CMacroUseHandler);
STATIC_DECORATOR("macro_export", CMacroExportHandler);
STATIC_DECORATOR("macro_reexport", CMacroReexportHandler);
STATIC_DECORATOR("rustc_builtin_macro", CBuiltinMacroHandler);
