#include "synext.h"

#include "synext.h"
#include "ast_expr.h"
#include "ast_ast.h"
#include "parse_common.h"
#include "parse_ttstream.h"
#include "ast_crate.h"
#include "macro_rules_macro_rules.h"
#include "hir_hir.h" // for HIR::Crate

class CMacroRulesExpander: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const ::AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        ERROR(sp, E0000, "macro_rules! requires an identifier");
    }

    ::std::unique_ptr<TokenStream> expandIdent(const Span& sp, const ::AST::Crate& crate, const RcString& ident, const TokenTree& tt, AST::Module& mod) override {
        DEBUG("Parsing macro_rules! " << ident);
        TTStream lex(sp, ParseState(), tt);
        auto mac = ParseMacroRules(lex);
        DEBUG("macro_rules! " << mod.path() + ident << " " << &*mac);
        mod.addMacro(false, ident, mv$(mac));

        return ::std::unique_ptr<TokenStream>(new TTStreamO(sp, ParseState(), TokenTree()));
    }
};

class CMacroUseHandler: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    bool run_during_iter() const override {
        return true;
    }

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        TRACE_FUNCTION_F("[CMacroUseHandler] path=" << path);

        std::vector<RcString> filter;
        if (mi.data().size() > 0) {
            mi.parse_paren_ident_list([&](const Span& sp, RcString ident) {
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
                ASSERT_BUG(sp, ec.hir->rootModule.macroItems.count(name) == 1, "Macro `" << name << "` missing from crate " << ec.mName);
                const auto* e = &*ec.hir->rootModule.macroItems.at(name);
                if (!e->publicity.is_global()) {
                    DEBUG("Not public: " << name);
                    continue;
                }

                AST::AbsolutePath path{ecItem->name, {name}};
                if (const auto* imp = e->ent.opt_Import()) {
                    if (imp->path.crate_name() == CRATE_BUILTINS) {
                        DEBUG("Importing builtin (skip): " << name);
                        continue;
                    }
                    ASSERT_BUG(sp, crate.externCrates.count(imp->path.crate_name()), "Crate `" << imp->path.crate_name() << "` not loaded");
                    const ::HIR::Module& mod = crate.externCrates.at(imp->path.crate_name()).hir->getModByPath(sp, imp->path, /*ignore_last_node*/ true, /*ignore_crate_name*/ true);

                    ASSERT_BUG(sp, mod.macroItems.count(imp->path.components().back()), "Failed to find final component of " << imp->path);
                    e = &*mod.macroItems.at(imp->path.components().back());
                    if (const auto& imp2 = e->ent.opt_Import()) {
                        if (imp2->path.crate_name() == CRATE_BUILTINS) {
                            DEBUG("Importing builtin (skip): " << name);
                            continue;
                        } else {
                            ASSERT_BUG(sp, !e->ent.is_Import(), "Recursive import - " << imp->path << " pointed to " << imp2->path);
                        }
                    } else {
                    }
                    path = AST::AbsolutePath(imp->path.crate_name(), imp->path.componentsVec());
                }

                MacroRef mr;
                TU_MATCH_HDRA( (e->ent), { )
                TU_ARMA(Import, imp) {
                        throw "Unexpected";
                    }
                    TU_ARMA(MacroRules, mac_ptr) {
                        mr = &*mac_ptr;
                    }
                    TU_ARMA(ProcMacro, p) {
                        mr = &p;
                    }
                }
                if(!exists(name, mr))
                {
                    auto mi = AST::Module::MacroImport{false, name, std::move(path), std::move(mr)};
                    DEBUG("Import macro " << mi.path);
                    mod.macroImports.push_back(mv$(mi));
                }
            }
        } else if (const auto* submod_p = i.opt_Module()) {
            const auto& submod = *submod_p;
            for (const auto& mr : submod.macros()) {
                if (!filterValid(mr.name)) {
                    continue;
                }
                DEBUG("Imported " << mr.name);
                if (!exists(mr.name, &*mr.data)) {
                    auto path = submod.path();
                    path.nodes.push_back(mr.name);
                    DEBUG(mod.path() << ": Import macro " << path);
                    mod.macroImports.push_back(AST::Module::MacroImport{false, mr.name, path, &*mr.data});
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
            WARNING(sp, W0000, "Use of #[macro_use] on non-module/crate - " << i.tag_str());
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

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        // TODO: Flags on the attribute
        // - `local_inner_macros`: Forces macro lookups within the expansion to search within the source crate
        //   > Strictly speaking, not the same as `macro`-style macros?
        bool local_inner_macros = false;
        if (mi.data().size() > 0) {
            mi.parse_paren_ident_list([&](const Span& sp, RcString ident) {
                if (ident == "local_inner_macros") {
                    local_inner_macros = true;
                } else {
                    ERROR(sp, E0000, "Unknown option for #[macro_export] - " << ident);
                }
            });
        }

        if (i.is_None()) {
        }
        // If on a `use` it's for a #[rustc_builtin_macro]
        else if (const auto* u = i.opt_Use()) {
            if (u->entries.size() == 1 && u->entries.back().path.is_absolute() && u->entries.back().path.cls.as_Absolute().crate == CRATE_BUILTINS && u->entries.back().path.cls.as_Absolute().nodes.size() == 1)
                ;
            else {
                ERROR(sp, E0000, "Use of #[macro_export] on non-macro - " << i.tag_str());
            }
            const auto& p = u->entries.back().path.cls.as_Absolute();
            const auto& name = p.nodes.front().name();
            mod.macroImports.push_back(AST::Module::MacroImport{true, u->entries.front().name, AST::AbsolutePath(p.crate, {name}), {}});

            crate.rootModule.addItem(sp, AST::Visibility::make_global(), name, i.clone(), {});
        } else if (i.is_MacroInv()) {
            const auto& mac = i.as_MacroInv();
            if (!(mac.path().is_trivial() && mac.path().asTrivial() == "macro_rules")) {
                ERROR(sp, E0000, "#[macro_export] is only valid on macro_rules!");
            }
            const auto& name = mac.input_ident();

            // Tag the macro in the module for crate export
            // AND move it to the root module
            auto it = ::std::find_if(mod.macros().begin(), mod.macros().end(), [&](const auto& x) {
                return x.name == name;
            });
            ASSERT_BUG(sp, it != mod.macros().end(), "Macro '" << name << "' not defined in this module");
            auto e = mv$(*it);
            mod.macros().erase(it);

            // Leave an alias here, so existing references are valid
            mod.macroImports.push_back(AST::Module::MacroImport{false, name, AST::AbsolutePath("", {name}), &*e.data});
            DEBUG(mod.path() << ": macro_use Import " << mod.macroImports.back().name << " = " << mod.macroImports.back().path);

            if (local_inner_macros) {
                Ident::ModPath mp;
                mp.crate = "";
                // Empty node list, will search the crate root
                // TODO: Strictly speaking, this shouldn't apply to non-macro paths
                DEBUG("#[macro_export(local_inner_macros)] mp=" << mp);
                e.data->mHygiene.set_mod_path(mv$(mp));
            }

            e.data->exported = true;
            DEBUG("- Export macro " << name << "!");
            crate.rootModule.macros().push_back(mv$(e));
        } else if (i.is_Macro()) {
            const auto& name = path.nodes.back();
            if (i.as_Macro()) {
                i.as_Macro()->exported = true;
                ASSERT_BUG(sp, path.nodes.size() == 1, "");
                DEBUG("- Export macro (item) " << name << "!");
                //crate.m_root_module.macros().push_back( mv$(*i.as_Macro()) );
            }
        } else {
            ERROR(sp, E0000, "Use of #[macro_export] on non-macro - " << i.tag_str());
        }
    }
};

class CMacroReexportHandler: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module&, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        if (!i.is_Crate()) {
            ERROR(sp, E0000, "Use of #[macro_reexport] on non-crate - " << i.tag_str());
        }

        const auto& crate_name = i.as_Crate().name;
        auto& ext_crate = *crate.externCrates.at(crate_name).hir;

        mi.parse_paren_ident_list([&](const Span& sp, RcString name) {
            auto it = ::std::find(ext_crate.exportedMacroNames.begin(), ext_crate.exportedMacroNames.end(), name);
            if (it == ext_crate.exportedMacroNames.end()) {
                ERROR(sp, E0000, "Could not find macro " << name << "! in crate " << crate_name);
            }
            // TODO: Do this differently.
            ext_crate.rootModule.macroItems.at(name)->ent.as_MacroRules()->exported = true;
            //ext_crate.m_root_module.m_macro_items.at(name)->publicity = AST::Publicity::new_global();
        });
    }
};

class CBuiltinMacroHandler: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& /*mod*/, size_t /*mod_idx*/, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        RcString name;
        if (i.is_MacroInv()) {
            const auto& e = i.as_MacroInv();
            if (!(e.path().is_trivial() && e.path().asTrivial() == "macro_rules")) {
                ERROR(sp, E0000, "Use of #[rustc_builtin_macro] on macro other than macro_rules! - " << i.tag_str());
            }
            name = e.input_ident();
        } else if (i.is_Macro()) {
            name = path.nodes.back();
        } else {
            ERROR(sp, E0000, "Use of #[rustc_builtin_macro] on non-macro - " << i.tag_str());
        }

        AST::UseItem ui;
        ui.entries.push_back(AST::UseItem::Ent{});
        ui.entries.back().name = name;
        ui.entries.back().path = AST::Path(RcString::new_interned(CRATE_BUILTINS), {name});
        DEBUG("Convert macro_rules tagged #[rustc_builtin_macro] with use - " << name);
        i = AST::Item::make_Use(mv$(ui));
    }
};

STATIC_MACRO("macro_rules", CMacroRulesExpander);
STATIC_DECORATOR("macro_use", CMacroUseHandler);
STATIC_DECORATOR("macro_export", CMacroExportHandler);
STATIC_DECORATOR("macro_reexport", CMacroReexportHandler);
STATIC_DECORATOR("rustc_builtin_macro", CBuiltinMacroHandler);
