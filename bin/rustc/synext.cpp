#include "synext.h"
#include "wire_board.h"
#include "synext.h"

#include "ast_ast.h"
#include "hir_hir.h" // for HIR::Crate
#include "ast_expr.h"
#include "ast_crate.h"
#include "expand_common.h"
#include "parse_common.h"
#include "parse_ttstream.h"
#include "macro_rules_macro_rules.h"

class CMacroRulesExpander: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override {
        ERROR(sp, E0000, "macro_rules! requires an identifier");
    }

    ::std::unique_ptr<TokenStream> expandIdent(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const RcString& ident, const TokenTree& tt, ASTModule& mod) override {
        DEBUG("Parsing macro_rules! " << ident);
        ParseState parseState;
        parseState.wb = &wb;
        parseState.crate = &crate;
        parseState.module = &mod;
        TTStream lex(sp, parseState, tt);
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
                switch (imp.ref.tag()) {
                    case MacroRef::TAG_None: {
                        rv = true;
                        break;
                    }
                    case MacroRef::TAG_MacroRules: {
                        auto& a = imp.ref.as_MacroRules();
                        auto& b = mr.as_MacroRules();
                        rv = (a == b);
                        break;
                    }
                    case MacroRef::TAG_BuiltinProcMacro: {
                        auto& a = imp.ref.as_BuiltinProcMacro();
                        auto& b = mr.as_BuiltinProcMacro();
                        rv = (a == b);
                        break;
                    }
                    case MacroRef::TAG_ExternalProcMacro: {
                        auto& a = imp.ref.as_ExternalProcMacro();
                        auto& b = mr.as_ExternalProcMacro();
                        rv = (a == b);
                        break;
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
                ASSERT_BUG(sp, ec.hir->rootModule.macroItems.count(name) == 1, "Macro `" << name << "` missing from crate " << ec.name);
                const auto* e = &*ec.hir->rootModule.macroItems.at(name);
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
                switch (e->ent.tag()) {
                    case HIRMacroItem::TAG_Import: {
                        UNREACHABLE();
                    }
                    case HIRMacroItem::TAG_MacroRules: {
                        auto& macPtr = e->ent.as_MacroRules();
                        mr = &*macPtr;
                        break;
                    }
                    case HIRMacroItem::TAG_ProcMacro: {
                        auto& p = e->ent.as_ProcMacro();
                        mr = &p;
                        break;
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

namespace {
    template<typename Contents>
    void localiseInnerMacroPaths(const WireBoard& wb, Contents& contents) {
        for (size_t i = 0; i < contents.size(); i++) {
            if (auto* loop = contents[i].opt_Loop()) {
                localiseInnerMacroPaths(wb, loop->entries);
                continue;
            }
            auto* token = contents[i].opt_Token();
            if (!token || token->type() != TOK_IDENT || i + 1 == contents.size()) {
                continue;
            }
            const auto* next = contents[i + 1].opt_Token();
            const auto* previous = i == 0 ? nullptr : contents[i - 1].opt_Token();
            if (!next || next->type() != TOK_EXCLAM || (previous && previous->type() == TOK_DOUBLE_COLON)) {
                continue;
            }

            auto position = token->getPos();
            auto ident = token->ident();
            Ident::ModPath mp;
            mp.crate = "";
            ident.hygiene.setModPath(*wb.pool, mp);
            *token = Token(TOK_IDENT, ident);
            token->setPos(position);
        }
    }

    bool macroExportUsesLocalInnerMacros(const ASTAttribute& attr) {
        bool localInnerMacros = false;
        if (attr.data().size() > 0) {
            attr.parseParenIdentList([&](const Span& sp, RcString ident) {
                if (ident == "local_inner_macros") {
                    localInnerMacros = true;
                } else {
                    ERROR(sp, E0000, "Unknown option for #[macro_export] - " << ident);
                }
            });
        }
        return localInnerMacros;
    }

    void exportMacroRules(const Span& sp, const WireBoard& wb, ASTCrate& crate, ASTModule& mod, const RcString& name, bool localInnerMacros) {
        auto it = ::std::find_if(mod.macros().begin(), mod.macros().end(), [&](const auto& x) {
            return x.name == name;
        });
        ASSERT_BUG(sp, it != mod.macros().end(), "Macro '" << name << "' not defined in this module");
        auto e = mv$(*it);
        mod.macros().erase(it);

        // Leave an alias here, so existing references are valid.
        mod.macroImports.push_back(ASTModule::MacroImport{false, name, ASTAbsolutePath("", {name}), &*e.data});
        DEBUG(mod.path() << ": macro_use Import " << mod.macroImports.back().name << " = " << mod.macroImports.back().path);

        if (localInnerMacros) {
            for (auto& rule : e.data->rules) {
                localiseInnerMacroPaths(wb, rule.contents);
            }
        }

        e.data->exported = true;
        DEBUG("- Export macro " << name << "!");
        crate.rootModule_.macros().push_back(mv$(e));
    }
}

void ExpandExportMacroRules(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, ASTModule& mod, const RcString& name) {
    exportMacroRules(sp, wb, crate, mod, name, macroExportUsesLocalInnerMacros(attr));
}

class CMacroExportHandler: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    bool runDuringIter() const override {
        return true;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        const bool localInnerMacros = macroExportUsesLocalInnerMacros(mi);

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

            crate.rootModule_.addItem(sp, ASTVisibility::makeGlobal(), name, i.clone(), {});
        } else if (i.is_MacroInv()) {
            const auto& mac = i.as_MacroInv();
            if (!(mac.path().isTrivial() && mac.path().asTrivial() == "macro_rules")) {
                ERROR(sp, E0000, "#[macro_export] is only valid on macro_rules!");
            }
            exportMacroRules(sp, wb, crate, mod, mac.inputIdent(), localInnerMacros);
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
            extCrate.rootModule.macroItems.at(name)->ent.as_MacroRules()->exported = true;
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

void RegisterSynextBuiltins(ExpandRegistry& registry) {
    registry.addMacro<CMacroRulesExpander>("macro_rules");
    registry.addDecorator<CMacroUseHandler>("macro_use");
    registry.addDecorator<CMacroExportHandler>("macro_export");
    registry.addDecorator<CMacroReexportHandler>("macro_reexport");
    registry.addDecorator<CBuiltinMacroHandler>("rustc_builtin_macro");
}
