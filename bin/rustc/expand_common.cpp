#include "expand_common.h"

#include "ast_ast.h"
#include "ast_crate.h"
#include "main_bindings.h"
#include "synext.h"
#include <map>
#include "macro_rules_macro_rules.h"
#include "parse_common.h" // For reparse from macros
#include "ast_expr.h"
#include "hir_hir.h" // For macro lookup
#include "expand_cfg.h"
#include "expand_common.h"
#include "resolve_common.h"
#include "expand_proc_macro.h"
#include "parse_ttstream.h"

// TODO: Respect the crate attribute #![recursion_limit]
#define MAX_MACRO_RECURSION 256

DecoratorDef* gDecoratorsList = nullptr;
MacroDef* gMacrosList = nullptr;
::std::map<RcString, ::std::unique_ptr<ExpandDecorator>> gDecorators;
::std::map<RcString, ::std::unique_ptr<ExpandProcMacro>> gMacros;
// HACK: Used for expanding proc macros, which need to re-parse without access to the current module
// - Parsing needs module for 1) anon modules, and 2) expanding `#[path]`
AST::Module* gCurrentMod = nullptr;

enum class ExpandMode {
    FirstPass,
    Iterate,
    Final,
};

struct ExpandState {
    ::AST::Crate& crate;
    LList<const AST::Module*> modstack;
    ExpandMode mode;
    mutable bool change;
    mutable bool hasMissing;

    ExpandState(::AST::Crate& crate, LList<const AST::Module*> modstack, ExpandMode mode)
        : crate(crate)
        , modstack(modstack)
        , mode(mode)
        , change(false)
        , hasMissing(false)
    {
        DEBUG("" << this);
    }

    explicit ExpandState(const ExpandState&) = default;
};

void ExpandAttrs(const ExpandState& es, const ::AST::AttributeList& attrs, AttrStage stage, ::std::function<void(const ExpandDecorator& d, const ::AST::Attribute& a)> f);
void ExpandMod(const ExpandState& es, ::AST::AbsolutePath modpath, ::AST::Module& mod, unsigned int firstItem = 0);
void ExpandExpr(const ExpandState& es, ::AST::ExprNodeP& node);
void ExpandExpr(const ExpandState& es, AST::Expr& node);
void ExpandExpr(const ExpandState& es, ::std::shared_ptr<AST::ExprNode>& node);
void ExpandPath(const ExpandState& es, ::AST::Module& mod, ::AST::Path& p);
void ExpandPathParams(const ExpandState& es, ::AST::Module& mod, ::AST::PathParams& params);

void RegisterSynextDecorator(::std::string name, ::std::unique_ptr<ExpandDecorator> handler) {
    gDecorators.insert(::std::make_pair(RcString::newInterned(name), mv$(handler)));
}

void RegisterSynextMacro(::std::string name, ::std::unique_ptr<ExpandProcMacro> handler) {
    gMacros.insert(::std::make_pair(RcString::newInterned(name), mv$(handler)));
}

void RegisterSynextDecoratorStatic(DecoratorDef* def) {
    def->prev = gDecoratorsList;
    gDecoratorsList = def;
}

void RegisterSynextMacroStatic(MacroDef* def) {
    def->prev = gMacrosList;
    gMacrosList = def;
}

void ExpandInit() {
    // TODO: Initialise all macros here.
    void ExpandInitAssert();
    ExpandInitAssert();
    void ExpandInitStdPrelude();
    ExpandInitStdPrelude();
    void ExpandInitPanic();
    ExpandInitPanic();

    // Fill macro/decorator map from init list
    while (gDecoratorsList) {
        gDecorators.insert(::std::make_pair(RcString::newInterned(gDecoratorsList->name), mv$(gDecoratorsList->def)));
        gDecoratorsList = gDecoratorsList->prev;
    }
    while (gMacrosList) {
        gMacros.insert(::std::make_pair(RcString::newInterned(gMacrosList->name), mv$(gMacrosList->def)));
        gMacrosList = gMacrosList->prev;
    }
}

void ExpandDecorator::unexpected(const Span& sp, const AST::Attribute& mi, const char* locStr) const {
    WARNING(sp, W0000, "Unexpected attribute " << mi.name() << " on " << locStr);
}

ExpandProcMacro* ExpandFindProcMacro(const RcString& name) {
    auto it = gMacros.find(name);
    return it != gMacros.end() ? it->second.get() : nullptr;
}

ExpandDecorator* ExpandFindDecorator(const RcString& name) {
    auto it = gDecorators.find(name);
    return it != gDecorators.end() ? it->second.get() : nullptr;
}

void ParseModRootItemsInto(AST::Module& mod, size_t idx, TokenStream& lex) {
    // Move the item list out
    auto oldItems = std::move(mod.mItems);
    // Parse module items
    ParseModRootItems(lex, mod);
    // Then insert the newly created items
    oldItems.insert(oldItems.begin() + idx + 1, std::make_move_iterator(mod.mItems.begin()), std::make_move_iterator(mod.mItems.end()));
    // and move the (updated) item list back in
    mod.mItems = std::move(oldItems);
}

void ExpandAttr(const ExpandState& es, const Span& sp, const ::AST::Attribute& a, AttrStage stage, ::std::function<void(const Span& sp, const ExpandDecorator& d, const ::AST::Attribute& a)> f) {
    bool found = false;
    if (a.name().elems.empty()) {
        return;
    }
    if (a.is_inert()) {
        return;
    }
    DEBUG(a);
    for (auto& d : gDecorators) {
        if (a.name() == d.first
            // HACK: Handle `::core::prelude::v1::<FOO>`
            || (a.name().elems.size() == 4 && a.name().elems[0] == "core" && a.name().elems[1] == "prelude" && a.name().elems[2] == "v1" && a.name().elems[3] == d.first)) {
            found = true;
            if (d.second->stage() != stage) {
                DEBUG("#[" << d.first << "] Ignore: Wrong stage " << (int)d.second->stage() << " != " << (int)stage);
            } else {
                if (!d.second->runDuringIter()) {
                    switch (es.mode) {
                        case ExpandMode::FirstPass:
                        case ExpandMode::Iterate:
                            if (stage != AttrStage::Pre) {
                                DEBUG("#[" << d.first << "] m=" << (int)es.mode);
                                continue;
                            }
                            break;
                            //continue ;
                        case ExpandMode::Final:
                            if (stage != AttrStage::Post) {
                                DEBUG("#[" << d.first << "] m=" << (int)es.mode);
                                continue;
                            }
                            break;
                    }
                }
                DEBUG("#[" << d.first << "]");
                f(sp, *d.second, a);
                // Annotate the attribute as having been handled
                a.markInert();
            }
        }
    }
    if (!found) {
        auto m = ExpandLookupMacro(sp, es.crate, es.modstack, a.name());
        DEBUG(a.name() << " : " << m.tag_str());
        if (m.is_None()) {
            // Ignore and error/warn at the bottom of the function
        } else if (const auto* procMacP = m.opt_ExternalProcMacro()) {
            const auto* procMac = *procMacP;

            struct ProcMacroDecorator: public ExpandDecorator {
                ::std::vector<RcString> macPath;

                AttrStage stage() const override {
                    return AttrStage::Pre;
                }

                bool runDuringIter() const override {
                    return false;
                }

                // Module item
                void handle(const Span& sp, const AST::Attribute& attr, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t modIdx, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
                    if (!i.is_None()) {
                        auto lex = ProcMacroInvoke(sp, crate, this->macPath, attr.data(), attrs, vis, path.nodes.back(), i);
                        if (lex) {
                            // TODO: `derive_where` returns its own attribute invocation in the output, between two other additions
                            //   > This seems to so the derive (first attribute) can see the trait list? (does mrustc handle that properly? I think so)
                            // - Could parse, and the locate the first matching item (same name?) and merge/filter its attributes
                            // - Should the rest of the attributes be applied
                            // - Tag the item (or item-range?) to stop it being able to invoke this macro again?
                            //  > OR, Should the derive macro be consuming the attribute?
                            // QUERY: Is it valid for there to be multiple items generated from an attribute proc macro?

                            // NOTE: The rust book isn't very clear on the details of how attribute macros work (especially with other/previous attributes)

                            i = AST::Item::make_None({});
                            lex->parse_state().module = &mod;
                            ParseModRootItemsInto(mod, modIdx, *lex);
                        } else {
                            ERROR(sp, E0000, "proc_macro expansion failed");
                        }
                    }
                }

                // Impl item
                void handle(const Span& sp, const AST::Attribute& attr, AST::Crate& crate, AST::Impl& impl, const RcString& name, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
                    if (!i.is_None()) {
                        auto lex = ProcMacroInvoke(sp, crate, this->macPath, attr.data(), attrs, vis, name, i);
                        if (lex) {
                            i = AST::Item::make_None({});
                            assert(gCurrentMod);
                            lex->parse_state().module = gCurrentMod;
                            while (lex->lookahead(0) != TOK_EOF) {
                                ParseImplItem(*lex, impl);
                            }
                        } else {
                            ERROR(sp, E0000, "proc_macro expansion failed");
                        }
                    }
                }
            } d;

            // Only run proc macros on first pass (before inner)
            if (stage == AttrStage::Pre) {
                d.macPath.push_back(procMac->path.crate_name());
                d.macPath.insert(d.macPath.end(), procMac->path.components().begin(), procMac->path.components().end());
                f(sp, d, a);
                a.markInert();
            }
            found = true;
        } else if (m.is_MacroRules()) {
            // Ignore
        } else {
            TODO(sp, "Attr " << a.name() << " : " << m.tag_str());
        }
    }
    if (!found) {
        // TODO: Create no-op handlers for a whole heap of attributes
        // - There's a LOT
        //WARNING(sp, W0000, "Unknown attribute #[" << a.name() << "]");
        //TODO(sp, "Unknown attribute #[" << a.name() << "]");
    }
}

void ExpandAttrs(const ExpandState& es, const ::AST::AttributeList& attrs, AttrStage stage, ::std::function<void(const Span& sp, const ExpandDecorator& d, const ::AST::Attribute& a)> f) {
    // Reduce load on derive etc by visiting `cfg` first.
    for (auto& a : attrs.mItems) {
        static const RcString rcstringCfg = RcString::newInterned("cfg");
        if (!a.is_inert() && a.name() == rcstringCfg) {
            ExpandAttr(es, a.span(), a, stage, f);
        }
    }
    for (auto& a : attrs.mItems) {
        ExpandAttr(es, a.span(), a, stage, f);
    }
}

void ExpandAttrsCfgAttr(AST::AttributeList& attrs) {
    for (auto it = attrs.mItems.begin(); it != attrs.mItems.end();) {
        auto& a = *it;
        static const RcString rcstringCfgAttr = RcString::newInterned("cfg_attr");
        if (a.name() == rcstringCfgAttr) {
            auto newAttrs = checkCfgAttr(a);
            it = attrs.mItems.erase(it);
            it = attrs.mItems.insert(it, std::make_move_iterator(newAttrs.begin()), std::make_move_iterator(newAttrs.end()));
        } else {
            ++it;
        }
    }
}

namespace {
    slice<const AST::Attribute> getAttrsAfter(const ::AST::AttributeList& attrs, const ::AST::Attribute& a) {
        const auto* start = &a + 1;
        const auto* end = &attrs.mItems.back() + 1;
        return slice<const AST::Attribute>(start, end - start);
    }
}

void ExpandAttrs(const ExpandState& es, const ::AST::AttributeList& attrs, AttrStage stage, const ::AST::AbsolutePath& path, ::AST::Module& mod, size_t modIdx, const AST::Visibility& vis, ::AST::Item& item) {
    ExpandAttrs(es, attrs, stage, [&](const Span& sp, const ExpandDecorator& d, const AST::Attribute& a) {
        if (!item.is_None()) {
            // Pass attributes _after_ this attribute (or all of them, if the decorator asks)
            auto attrsSlice = d.wants_all_attrs() ? slice<const AST::Attribute>(attrs.mItems.data(), attrs.mItems.size()) : getAttrsAfter(attrs, a);
            d.handle(sp, a, es.crate, path, mod, modIdx, attrsSlice, vis, item);
        }
    });
}

void ExpandAttrs(const ExpandState& es, const ::AST::AttributeList& attrs, AttrStage stage, const ::AST::AbsolutePath& path, ::AST::Module& mod, ::AST::Trait& trait, ::AST::Item& item) {
    gCurrentMod = &mod;
    ExpandAttrs(es, attrs, stage, [&](const Span& sp, const auto& d, const AST::Attribute& a) {
        if (!item.is_None()) {
            d.handle(sp, a, es.crate, path, trait, getAttrsAfter(attrs, a), item);
        }
    });
    gCurrentMod = nullptr;
}

void ExpandAttrs(const ExpandState& es, const ::AST::AttributeList& attrs, AttrStage stage, ::AST::Module& mod, ::AST::Impl& impl, const AST::Visibility& vis, const RcString& name, ::AST::Item& item) {
    gCurrentMod = &mod;
    ExpandAttrs(es, attrs, stage, [&](const Span& sp, const auto& d, const auto& a) {
        if (!item.is_None()) {
            d.handle(sp, a, es.crate, impl, name, getAttrsAfter(attrs, a), vis, item);
        }
    });
    gCurrentMod = nullptr;
}

bool ExpandAttrsCfgOnly(const ExpandState& es, AST::AttributeList& attrs) {
    bool remove = false;
    ExpandAttrsCfgAttr(attrs);
    ExpandAttrs(es, attrs, AttrStage::Pre, [&](const Span& sp, const ExpandDecorator& d, const AST::Attribute& a) {
        if (a.name() == "cfg") {
            if (!checkCfg(sp, a)) {
                remove = true;
            }
            return;
        } else if (a.name() == "allow") {
            // Lazy allow allow
        } else {
            TODO(sp, "non-cfg attributes - " << a);
        }
    });
    return !remove;
}

MacroRef ExpandLookupMacro(const Span& miSpan, const ::AST::Crate& crate, LList<const AST::Module*> modstack, const AST::AttributeName& path) {
    AST::Path p = AST::Path::newRelative({}, {});
    for (const auto& ent : path.elems) {
        p += AST::PathNode(ent);
    }
    return ExpandLookupMacro(miSpan, crate, modstack, p);
}

MacroRef ExpandLookupMacro(const Span& miSpan, const ::AST::Crate& crate, LList<const AST::Module*> modstack, const AST::Path& path) {
    ASSERT_BUG(miSpan, path.size() > 0, "Path should have nodes: " << path);

    if (path.is_trivial()) {
        const auto& name = path.asTrivial();

        // Iterate up the module tree, using the first located macro
        for (const auto* ll = &modstack; ll; ll = ll->prev) {
            if (!ll->item) {
                DEBUG("null module in stack");
                break;
            }
            const auto& macMod = *ll->item;
            DEBUG("Searching in " << macMod.path());
            for (const auto& mr : reverse(macMod.macros())) {
                if (mr.name == name) {
                    DEBUG(macMod.path() << "::" << mr.name << " - Defined");
                    return MacroRef(&*mr.data);
                }
            }

            // Find the last macro of this name (allows later #[macro_use] definitions to override)
            MacroRef rv;
            for (const auto& mri : macMod.macroImports) {
                //DEBUG("- " << mri.name);
                if (mri.name == name) {
                    DEBUG("?::" << mri.name << " - Imported");
                    rv = mri.ref.clone();
                }
            }
            if (!rv.is_None()) {
                return rv;
            }
        }
        // Search compiler-provided proc macros (after locals)
        if (auto* pm = ExpandFindProcMacro(name)) {
            DEBUG("Found builtin");
            return MacroRef(pm);
        }
        if (path.cls.is_Local()) {
            DEBUG("Local path not resolved?");
            return MacroRef();
        }
    }

    // HACK: If the crate name is empty, look up builtins
    if (path.is_absolute() && path.cls.as_Absolute().crate == "" && path.nodes().size() == 1) {
        const auto& name = path.nodes()[0].name();
        if (auto* pm = ExpandFindProcMacro(name)) {
            return MacroRef(pm);
        }
    }

    // Resolve the path, following use statements (if required)
    // - Only mr_ptr matters, as proc_mac is about builtins
    auto rv = ResolveLookupMacro(miSpan, crate, modstack.item->path(), path, /*out_path=*/nullptr);
    TU_MATCH_HDRA( (rv), { )
    TU_ARMA(None, _e)
        return MacroRef();
        TU_ARMA(InternalMacro, pm)
        return pm;
        TU_ARMA(ProcMacro, pm)
        return pm;
        TU_ARMA(MacroRules, p)
        return p;
    }
    return MacroRef();
}

::std::unique_ptr<TokenStream> ExpandMacroInner(const ::AST::Crate& crate, LList<const AST::Module*> modstack, ::AST::Module& mod, Span miSpan, const AST::Path& path, const RcString& inputIdent, TokenTree& input_tt) {
    ASSERT_BUG(miSpan, path.isValid(), "Macro invocation with invalid path");

    TRACE_FUNCTION_F("Searching for macro " << path);

    // Find the macro
    auto mac = ExpandLookupMacro(miSpan, crate, modstack, path);
    if (mac.is_MacroRules()) {
        // TODO: If `mr_ptr` is tagged with #[rustc_builtin_macro], look for a matching entry in `g_macros`
    }

    ::std::unique_ptr<TokenStream> rv;
    TU_MATCH_HDRA( (mac), {)
    TU_ARMA(None, e) {
            DEBUG("Unknown macro " << path);
            return ::std::unique_ptr<TokenStream>();
        }
        TU_ARMA(ExternalProcMacro, procMac) {
            ::std::vector<RcString> macPath;
            macPath.push_back(procMac->path.crate_name());
            macPath.insert(macPath.end(), procMac->path.components().begin(), procMac->path.components().end());
            rv = ProcMacroInvoke(miSpan, crate, macPath, input_tt);
        }
        TU_ARMA(BuiltinProcMacro, procMac) {
            ASSERT_BUG(miSpan, procMac, "null BuiltinProcMacro? " << path);
            rv = inputIdent == "" ? procMac->expand(miSpan, crate, input_tt, mod) : procMac->expandIdent(miSpan, crate, inputIdent, input_tt, mod);
        }
        TU_ARMA(MacroRules, mrPtr) {
            if (inputIdent != "") {
                ERROR(miSpan, E0000, "macro_rules! macros can't take an ident");
            }

            DEBUG("Invoking macro_rules " << path << " " << mrPtr);
            rv = MacroInvokeRules(path.is_trivial() ? path.asTrivial() : RcString::newInterned(FMT(path).c_str()), *mrPtr, miSpan, mv$(input_tt), crate, mod);
            input_tt = TokenTree();
        }
    }
    ASSERT_BUG(miSpan, rv, "Macro invocation returned null tokentree");
    return rv;
}

::std::unique_ptr<TokenStream> ExpandMacro(const ExpandState& es, ::AST::Module& mod, Span miSpan, const AST::Path& path, const RcString& inputIdent, TokenTree& input_tt) {
    auto rv = ExpandMacroInner(es.crate, es.modstack, mod, miSpan, path, inputIdent, input_tt);
    if (rv) {
        es.change = true;
        DEBUG("Change flagged");
        rv->parse_state().crate = &es.crate;
        rv->parse_state().module = &mod;
    } else {
        // HACK: Allow the expansion to happen, even in final (e.g. if from derive)
        if (es.mode == ExpandMode::Final) {
            ERROR(miSpan, E0000, "Unknown macro " << path);
        }
        DEBUG("Missing, waiting until another pass (set es.has_missing=true)");
        es.hasMissing = true;
    }
    return rv;
}

::std::unique_ptr<TokenStream> ExpandMacro(const ExpandState& es, ::AST::Module& mod, ::AST::MacroInvocation& mi) {
    return ExpandMacro(es, mod, mi.span(), mi.path(), mi.inputIdent(), mi.input_tt());
}

void ExpandPattern(const ExpandState& es, ::AST::Module& mod, ::AST::Pattern& pat, bool isRefutable) {
    TU_MATCH_HDRA( (pat.data()), {)
    TU_ARMA(MaybeBind, e) {
        }
        TU_ARMA(Macro, e) {
            const auto span = e.inv->span();

            auto tt = ExpandMacro(es, mod, *e.inv);
            if (tt) {
                auto& lex = *tt;
                auto newpat = ParsePattern(lex);
                if (LOOK_AHEAD(lex) != TOK_EOF) {
                    ERROR(span, E0000, "Trailing tokens in macro expansion");
                }

                for (auto& b : pat.bindings()) {
                    newpat.bindings().push_back(std::move(b));
                }

                pat = mv$(newpat);
                ExpandPattern(es, mod, pat, isRefutable);
            }
        }
        TU_ARMA(Any, e) {
        }
        TU_ARMA(Box, e) {
            ExpandPattern(es, mod, *e.sub, isRefutable);
        }
        TU_ARMA(Ref, e) {
            ExpandPattern(es, mod, *e.sub, isRefutable);
        }
        TU_ARMA(Value, e) {
            //Expand_Expr(crate, modstack, e.start);
            //Expand_Expr(crate, modstack, e.end);
        }
        TU_ARMA(ValueLeftInc, e) {
            //Expand_Expr(crate, modstack, e.start);
            //Expand_Expr(crate, modstack, e.end);
        }
        TU_ARMA(Tuple, e) {
            for (auto& sp : e.start) {
                ExpandPattern(es, mod, sp, isRefutable);
            }
            for (auto& sp : e.end) {
                ExpandPattern(es, mod, sp, isRefutable);
            }
        }
        TU_ARMA(StructTuple, e) {
            for (auto& sp : e.tup_pat.start) {
                ExpandPattern(es, mod, sp, isRefutable);
            }
            for (auto& sp : e.tup_pat.end) {
                ExpandPattern(es, mod, sp, isRefutable);
            }
        }
        TU_ARMA(Struct, e) {
            for (auto& subpat : e.sub_patterns) {
                if (!ExpandAttrsCfgOnly(es, subpat.attrs)) {
                    subpat.name = RcString();
                    continue;
                }

                ExpandPattern(es, mod, subpat.pat, isRefutable);
            }
            auto newEnd = std::remove_if(e.sub_patterns.begin(), e.sub_patterns.end(), [&](const auto& e) {
                return e.name == "";
            });
            e.sub_patterns.erase(newEnd, e.sub_patterns.end());
        }
        TU_ARMA(Slice, e) {
            for (auto& sp : e.sub_pats) {
                ExpandPattern(es, mod, sp, isRefutable);
            }
        }
        TU_ARMA(SplitSlice, e) {
            for (auto& sp : e.leading) {
                ExpandPattern(es, mod, sp, isRefutable);
            }
            for (auto& sp : e.trailing) {
                ExpandPattern(es, mod, sp, isRefutable);
            }
        }
        TU_ARMA(Or, e) {
            for (auto& sp : e) {
                ExpandPattern(es, mod, sp, isRefutable);
            }
        }
    }
}

void ExpandType(const ExpandState& es, ::AST::Module& mod, ::TypeRef& ty) {
    TU_MATCH_HDRA( (ty.mData), {)
    TU_ARMA(None, e) {
        }
        TU_ARMA(Any, e) {
        }
        TU_ARMA(Unit, e) {
        }
        TU_ARMA(Bang, e) {
        }
        TU_ARMA(Macro, e) {
            auto tt = ExpandMacro(es, mod, *e.inv);
            if (tt) {
                auto newTy = ParseType(*tt);
                if (tt->lookahead(0) != TOK_EOF) {
                    ERROR(e.inv->span(), E0000, "Extra tokens after parsed type");
                }
                ty = mv$(newTy);

                ExpandType(es, mod, ty);
            }
        }
        TU_ARMA(Primitive, e) {
        }
        TU_ARMA(Function, e) {
            TypeFunction& tf = e.info;
            ExpandType(es, mod, *tf.mRettype);
            for (auto& st : tf.argTypes) {
                ExpandType(es, mod, st);
            }
        }
        TU_ARMA(Tuple, e) {
            for (auto& st : e.innerTypes) {
                ExpandType(es, mod, st);
            }
        }
        TU_ARMA(Borrow, e) {
            ExpandType(es, mod, *e.inner);
        }
        TU_ARMA(Pointer, e) {
            ExpandType(es, mod, *e.inner);
        }
        TU_ARMA(Array, e) {
            ExpandType(es, mod, *e.inner);
            if (e.size) {
                ExpandExpr(es, e.size);
            }
        }
        TU_ARMA(Slice, e) {
            ExpandType(es, mod, *e.inner);
        }
        TU_ARMA(Generic, e) {
        }
        TU_ARMA(Path, e) {
            ExpandPath(es, mod, *e);
        }
        TU_ARMA(TraitObject, e) {
            for (auto& p : e.traits) {
                // TODO: p.hrbs? Not needed until types are in those
                ExpandPath(es, mod, *p.path);
            }
        }
        TU_ARMA(ErasedType, e) {
            for (auto& p : e->traits) {
                // TODO: p.hrbs?
                ExpandPath(es, mod, *p.path);
            }
            for (auto& p : e->maybeTraits) {
                // TODO: p.hrbs?
                ExpandPath(es, mod, *p.path);
            }
            if (e->use) {
                ExpandPathParams(es, mod, *e->use);
            }
        }
    }
}

void ExpandPathParams(const ExpandState& es, ::AST::Module& mod, ::AST::PathParams& params) {
    for (auto& e : params.entries) {
        TU_MATCH_HDRA( (e), {)
        TU_ARMA(Null, _) {
            }
            TU_ARMA(Lifetime, _) {
            }
            TU_ARMA(Type, typ) {
                ExpandType(es, mod, typ);
            }
            TU_ARMA(Value, node) {
                ExpandExpr(es, node);
            }
            TU_ARMA(AssociatedTyEqual, aty) {
                ExpandPathParams(es, mod, aty.first.args());
                ExpandType(es, mod, aty.second);
            }
            TU_ARMA(AssociatedTyBound, aty) {
                ExpandPathParams(es, mod, aty.first.args());
                for (auto& p : aty.second) {
                    ExpandPath(es, mod, p);
                }
            }
        }
    }
}

void ExpandPath(const ExpandState& es, ::AST::Module& mod, ::AST::Path& p) {
    auto expandNodes = [&](::std::vector<::AST::PathNode>& nodes) {
        for (auto& node : nodes) {
            ExpandPathParams(es, mod, node.args());
        }
    };

    TU_MATCH_HDRA( (p.cls), {)
    TU_ARMA(Invalid, pe) {
        }
        TU_ARMA(Local, pe) {
        }
        TU_ARMA(Relative, pe) {
            expandNodes(pe.nodes);
        }
        TU_ARMA(Self, pe) {
            expandNodes(pe.nodes);
        }
        TU_ARMA(Super, pe) {
            expandNodes(pe.nodes);
        }
        TU_ARMA(Absolute, pe) {
            expandNodes(pe.nodes);
        }
        TU_ARMA(UFCS, pe) {
            ExpandType(es, mod, *pe.type);
            if (pe.trait) {
                ExpandPath(es, mod, *pe.trait);
            }
            expandNodes(pe.nodes);
        }
    }
}

namespace {
    static AST::Path getPath(const RcString& coreCrate, const char* c1, const char* c2) {
        return AST::AbsolutePath(coreCrate, {RcString::newInterned(c1), RcString::newInterned(c2)});
    }

    static AST::Path getPath(const RcString& coreCrate, const char* c1, const char* c2, const char* c3) {
        return AST::AbsolutePath(coreCrate, {RcString::newInterned(c1), RcString::newInterned(c2), RcString::newInterned(c3)});
    }
}

struct CExpandExpr: public ::AST::NodeVisitor {
    ::AST::Crate& crate;
    const ExpandState& parentExpandState;
    ExpandState expandState;
    ::AST::ExprNodeP replacement;

    // Stack of `try { ... }` blocks (the string is the loop label for the desugaring)
    ::std::vector<RcString> tryStack;
    unsigned tryIndex = 0;

    AST::ExprNodeBlock* current_block = nullptr;
    bool inAssignLhs = false;

    CExpandExpr(const ExpandState& es)
        : crate(es.crate)
        , parentExpandState(es)
        , expandState(es)
    {
    }

    ~CExpandExpr() {
        if (expandState.change) {
            if (!parentExpandState.change) {
                DEBUG("Propagate change");
            }
            parentExpandState.change = true;
        }
        if (expandState.hasMissing) {
            if (!parentExpandState.hasMissing) {
                DEBUG("Propagate has missing (to " << &parentExpandState << ")");
            }
            parentExpandState.hasMissing = true;
        }
    }

    ::AST::Module& curMod() {
        return *const_cast<::AST::Module*>(expandState.modstack.item);
    }

    void visit(::AST::ExprNodeP& cnode) {
        if (cnode.get()) {
            auto attrs = mv$(cnode->attrs());
            ExpandAttrsCfgAttr(attrs);
            ExpandAttrs(expandState, attrs, AttrStage::Pre, [&](const Span& sp, const ExpandDecorator& d, const auto& a) {
                d.handle(sp, a, this->crate, cnode);
            });
            if (cnode.get()) {
                cnode->attrs() = mv$(attrs);
            }
        }
        if (cnode.get()) {
            cnode->visit(*this);
            // If the node was a macro, and it was consumed, reset it
            if (auto* nMac = cast<AST::ExprNodeMacro>(cnode.get())) {
                if (!nMac->mPath.isValid()) {
                    cnode.reset();
                }
            }
            if (this->replacement) {
                cnode = mv$(this->replacement);
            }
        }

        if (cnode.get()) {
            auto attrs = mv$(cnode->attrs());
            ExpandAttrs(expandState, attrs, AttrStage::Post, [&](const Span& sp, const auto& d, const auto& a) {
                d.handle(sp, a, this->crate, cnode);
            });
            if (cnode.get()) {
                cnode->attrs() = mv$(attrs);
            }
        }
        assert(!this->replacement);
    }

    void visit_nodelete(const ::AST::ExprNode& parent, ::AST::ExprNodeP& cnode) {
        if (cnode.get() != nullptr) {
            this->visit(cnode);
            if (cnode.get() == nullptr) {
                ERROR(parent.span(), E0000, "#[cfg] not allowed in this position");
            }
        }
        assert(!this->replacement);
    }

    void visit_vector(::std::vector<::AST::ExprNodeP>& cnodes) {
        for (auto it = cnodes.begin(); it != cnodes.end();) {
            assert(it->get());
            this->visit(*it);
            if (it->get() == nullptr) {
                it = cnodes.erase(it);
            } else {
                ++it;
            }
        }
    }

    ::AST::ExprNodeP visit_macro(::AST::ExprNodeMacro& node, ::std::vector<::AST::ExprNodeBlock::Line>* nodesOut) {
        TRACE_FUNCTION_F(node.mPath << "!");
        if (!node.mPath.isValid()) {
            return ::AST::ExprNodeP();
        }

        const bool definesMacro = node.mPath.is_trivial() && node.mPath.asTrivial() == "macro_rules";

        ::AST::ExprNodeP rv;
        auto& mod = this->curMod();
        auto ttl = ExpandMacro(expandState, mod, node.span(), node.mPath, node.ident, node.tokens);
        if (!ttl.get()) {
            // No expansion
            DEBUG("Deferred");
        } else {
            if (definesMacro) {
                auto it = ::std::find_if(mod.macros().rbegin(), mod.macros().rend(), [&](const auto& macro) {
                    return macro.name == node.ident;
                });
                ASSERT_BUG(node.span(), it != mod.macros().rend(), "macro_rules! definition was not installed");
                it->data->definitionHygiene = node.definitionHygiene;
                if (nodesOut) {
                    auto marker = ::AST::ExprNodeP(new ::AST::ExprNodeMacroDefinition(
                        it->data->definitionId,
                        it->data->mHygiene,
                        it->data->definitionHygiene
                    ));
                    marker->set_span(node.span());
                    nodesOut->push_back({true, ::std::move(marker)});
                }
            }
            while (ttl->lookahead(0) != TOK_EOF) {
                SET_MODULE((*ttl), mod);

                // Reparse as expression / item
                bool addSilenceIfEnd = false;
                ::std::shared_ptr<AST::Module> tmp_local_mod;
                auto& localModPtr = (this->current_block ? this->current_block->localMod : tmp_local_mod);
                DEBUG("-- Parsing as expression line");
                auto newexpr = ParseExprBlockLineWithItems(*ttl, localModPtr, addSilenceIfEnd);

                if (tmp_local_mod) {
                    TODO(node.span(), "Handle edge case where a macro expansion outside of a _Block creates an item");
                }

                if (newexpr) {
                    if (nodesOut) {
                        nodesOut->push_back({addSilenceIfEnd, mv$(newexpr)});
                    } else {
                        assert(!rv);
                        rv = mv$(newexpr);
                    }
                } else {
                    // Expansion line just added a new item
                }

                if (ttl->lookahead(0) != TOK_EOF) {
                    if (!nodesOut) {
                        ERROR(node.span(), E0000, "Unused tokens at the end of macro expansion - " << ttl->getToken());
                    }
                }
            }
            node.mPath = AST::Path();

            if (!nodesOut && !rv) {
                ERROR(node.span(), E0000, "Macro didn't expand to anything");
            }
        }

        return mv$(rv);
    }

    void visit(::AST::ExprNodeMacro& node) override {
        TRACE_FUNCTION_F("ExprNode_Macro - name = " << node.mPath);
        if (!node.mPath.isValid()) {
            return;
        }

        replacement = this->visit_macro(node, nullptr);

        if (this->replacement) {
            DEBUG("--- Visiting new node");
            auto n = mv$(this->replacement);
            this->visit(n);
            if (n) {
                assert(!this->replacement);
                this->replacement = mv$(n);
            }
        }
    }

    void visit(::AST::ExprNodeBlock& node) override {
        this->inAssignLhs = false;
        unsigned int modItemCount = 0;

        auto prevModstack = this->expandState.modstack;
        if (node.localMod) {
            this->expandState.modstack = LList<const ::AST::Module*>(&prevModstack, node.localMod.get());
        }

        // TODO: macro_rules! invocations within the expression list influence this.
        // > Solution: Defer creation of the local module until during expand.
        if (node.localMod) {
            ExpandMod(this->expandState, node.localMod->path(), *node.localMod);
            modItemCount = node.localMod->mItems.size();
        }

        auto saved = this->current_block;
        this->current_block = &node;

        for (auto it = node.nodes.begin(); it != node.nodes.end();) {
            assert(it->node.get());

            if (auto* nodeMac = cast<::AST::ExprNodeMacro>(it->node.get())) {
                auto attrs = std::move(it->node->attrs());
                ExpandAttrsCfgAttr(attrs);
                ExpandAttrs(expandState, attrs, AttrStage::Pre, [&](const Span& sp, const auto& d, const auto& a) {
                    d.handle(sp, a, this->crate, it->node);
                });
                if (!it->node.get()) {
                    it = node.nodes.erase(it);
                    continue;
                }
                it->node->attrs() = std::move(attrs);

                assert(it->node.get() == nodeMac);

                ::std::vector<::AST::ExprNodeBlock::Line> new_nodes;
                this->visit_macro(*nodeMac, &new_nodes);
                for (const auto& n : new_nodes) {
                    DEBUG("++ " << *n.node << (n.hasSemicolon ? " ;" : ""));
                }

                if (nodeMac->mPath.isValid()) {
                    DEBUG("Deferred macro");
                    ++it;
                } else {
                    // If this has a semicolon, then force the new final node to have a semicolon
                    if (it->hasSemicolon && !new_nodes.empty()) {
                        new_nodes.back().hasSemicolon = true;
                    }
                    it = node.nodes.erase(it);
                    it = node.nodes.insert(it, ::std::make_move_iterator(new_nodes.begin()), ::std::make_move_iterator(new_nodes.end()));
                    // NOTE: Doesn't advance the iterator above, we want to re-visit the new node
                }
            } else {
                this->visit(it->node);
                if (it->node.get() == nullptr) {
                    it = node.nodes.erase(it);
                } else {
                    ++it;
                }
            }
        }

        this->current_block = saved;

        // HACK! Run Expand_Mod twice on local modules.
        if (node.localMod) {
            ExpandMod(this->expandState, node.localMod->path(), *node.localMod, modItemCount);
        }

        this->expandState.modstack = mv$(prevModstack);
    }

    void visit(::AST::ExprNodeAsyncBlock& node) override {
        this->visit_nodelete(node, node.inner);
    }

    void visit(::AST::ExprNodeGeneratorBlock& node) override {
        this->visit_nodelete(node, node.inner);
    }

    void visit(::AST::ExprNodeTry& node) override {
        // Desugar into
        // ```
        // loop '#tryNNN {
        //   break '#tryNNN { ... }
        // }
        // ```
        // NOTE: MIR lowering and HIR typecheck need to know to skip these (OR resolve should handle naming all loop blocks)
        tryStack.push_back(RcString::newInterned(FMT("#try" << tryIndex++)));
        this->visit_nodelete(node, node.inner);
        auto loopName = mv$(tryStack.back());
        tryStack.pop_back();

        auto coreCrate = crate.extCratenameCore;
        auto pathTry = getPath(coreCrate, "ops", "Try");
        auto pathTryFromOutput = ::AST::Path::newUfcsTrait(::TypeRef(node.span()), pathTry, {::AST::PathNode(RcString::newInterned("from_output"))});
        auto okNode = ::AST::ExprNodeP(new ::AST::ExprNodeCallPath(mv$(pathTryFromOutput), ::makeVec1(mv$(node.inner))));
        auto breakNode = AST::ExprNodeP(new AST::ExprNodeFlow(AST::ExprNodeFlow::BREAK, loopName, mv$(okNode)));
        this->replacement = AST::ExprNodeP(new AST::ExprNodeLoop(loopName, mv$(breakNode)));
    }

    void visit(::AST::ExprNodeAsm& node) override {
        for (auto& v : node.output) {
            this->visit_nodelete(node, v.value);
        }
        for (auto& v : node.input) {
            this->visit_nodelete(node, v.value);
        }
    }

    void visit(::AST::ExprNodeAsm2& node) override {
        for (auto& v : node.mParams) {
            TU_MATCH_HDRA((v), {)
            TU_ARMA(Const, e) {
                    this->visit_nodelete(node, e);
                }
                TU_ARMA(Sym, e) {
                    ExpandPath(this->expandState, this->curMod(), e);
                }
                TU_ARMA(RegSingle, e) {
                    this->visit_nodelete(node, e.val);
                }
                TU_ARMA(Reg, e) {
                    this->visit_nodelete(node, e.val_in);
                    this->visit_nodelete(node, e.val_out);
                }
            }
        }
    }

    void visit(::AST::ExprNodeFlow& node) override {
        this->visit_nodelete(node, node.mValue);

        if (node.mType == AST::ExprNodeFlow::YEET) {
            auto coreCrate = crate.extCratenameCore;
            auto pathOpsYeet = getPath(coreCrate, "ops", "Yeet");
            auto pathFromResidualFromResidual = getPath(coreCrate, "ops", "FromResidual", "from_residual");

            auto v = ::AST::ExprNodeP(new ::AST::ExprNodeCallPath(::AST::Path(pathOpsYeet), ::makeVec1(std::move(node.mValue))));
            v->set_span(node.span());
            v = ::AST::ExprNodeP(new ::AST::ExprNodeCallPath(::AST::Path(pathFromResidualFromResidual), ::makeVec1(std::move(v))));
            v->set_span(node.span());
            replacement = ::AST::ExprNodeP(new ::AST::ExprNodeFlow(
                (tryStack.empty() ? ::AST::ExprNodeFlow::RETURN : ::AST::ExprNodeFlow::BREAK), // NOTE: uses `break 'tryblock` instead of return if in a try block.
                (tryStack.empty() ? RcString("") : tryStack.back()),
                std::move(v)
            ));
            replacement->set_span(node.span());
        }
    }

    void visit(::AST::ExprNodeLetBinding& node) override {
        ExpandType(this->expandState, this->curMod(), node.mType);
        ExpandPattern(this->expandState, this->curMod(), node.pat, false);
        this->visit_nodelete(node, node.mValue);
        this->visit_nodelete(node, node.elseNode);
    }

    void visit(::AST::ExprNodeAssign& node) override {
        inAssignLhs = true;
        this->visit_nodelete(node, node.slot);
        inAssignLhs = false;
        this->visit_nodelete(node, node.mValue);

        // Desugar destructuring assignment
        // https://rust-lang.github.io/rfcs/2909-destructuring-assignment.html
        if (node.op == ::AST::ExprNodeAssign::NONE) {
            struct VisitorToPat: public ::AST::NodeVisitor {
                std::vector<std::pair<RcString, AST::ExprNodeP>> slots;
                ::AST::Pattern mRv;

                bool mRvSet = false;
                bool mIsSlot = false;

                ::AST::Pattern lower(::AST::ExprNodeP& ep) {
                    assert(ep);
                    ep->visit(*this);
                    ASSERT_BUG(ep->span(), mRvSet, ep.type_name() << " - Didn't yield a pattern");
                    if (mIsSlot) {
                        assert(!slots.empty());
                        assert(!slots.back().second);
                        slots.back().second = std::move(ep);
                        mIsSlot = false;
                    }
                    mRvSet = false;
                    return std::move(mRv);
                }

                // - This is a de-structuring pattern
                void pat(::AST::Pattern rv) {
                    assert(!mRvSet);
                    assert(!mIsSlot);
                    mRvSet = true;
                    assert(rv.bindings().empty());
                    mRv = std::move(rv);
                }

                // - This is a slot (to be assigned)
                void slot(::AST::ExprNode& v) {
                    mRvSet = true;
                    mIsSlot = true;

                    RcString name(FMT("_#" << slots.size()).c_str());
                    slots.push_back(std::make_pair(name, AST::ExprNodeP()));
                    mRv = AST::Pattern(AST::Pattern::TagBind(), v.span(), slots.back().first);
                }

                // - The given node isn't valid on the LHS of an assignment
                void invalid(const ::AST::ExprNode& v) {
                    ERROR(v.span(), E0000, typeid(v).name() << " isn't valid on the LHS of an assignemnt");
                }

                void visit(::AST::ExprNodeBlock& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeAsyncBlock& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeGeneratorBlock& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeTry& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeMacro& v) override {
                    BUG(v.span(), "Encountered macro");
                }

                void visit(::AST::ExprNodeAsm& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeAsm2& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeFlow& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeLetBinding& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeAssign& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeCallPath& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeCallMethod& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeCallObject& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeLoop& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeFor& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeWhile& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeMatch& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeIf& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeWildcardPattern& v) override {
                    mRvSet = true;
                    mRv = AST::Pattern(v.span(), AST::Pattern::Data::make_Any({}));
                }

                void visit(::AST::ExprNodeInteger& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeFloat& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeBool& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeString& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeByteString& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeCString& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeClosure& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeStructLiteral& v) override {
                    if (v.baseValue) {
                        TODO(v.span(), "Struct literal with `..value` in destructured assignment");
                    }
                    std::vector<AST::StructPatternEntry> subpats;
                    for (auto& m : v.values) {
                        subpats.push_back(AST::StructPatternEntry{std::move(m.attrs), m.name, lower(m.value)});
                    }
                    pat(AST::Pattern(AST::Pattern::TagStruct(), v.span(), v.mPath, std::move(subpats), true));
                }

                void visit(::AST::ExprNodeStructLiteralPattern& v) override {
                    std::vector<AST::StructPatternEntry> subpats;
                    for (auto& m : v.values) {
                        subpats.push_back(AST::StructPatternEntry{std::move(m.attrs), m.name, lower(m.value)});
                    }
                    pat(AST::Pattern(AST::Pattern::TagStruct(), v.span(), v.mPath, std::move(subpats), false));
                }

                void visit(::AST::ExprNodeArray& v) override {
                    if (v.mSize) {
                        TODO(v.span(), "Sized Array literal in destructured assignment");
                    } else {
                        std::vector<AST::Pattern> subpats;
                        for (auto& m : v.values) {
                            subpats.push_back(lower(m));
                        }
                        pat(AST::Pattern(v.span(), AST::Pattern::Data::make_Slice({std::move(subpats)})));
                    }
                }

                void visit(::AST::ExprNodeTuple& v) override {
                    bool isSplit = false;
                    std::vector<AST::Pattern> subpats_start;
                    std::vector<AST::Pattern> subpats;
                    for (auto& m : v.values) {
                        if (const auto* e = cast<AST::ExprNodeBinOp>(m.get())) {
                            if (e->mType == ::AST::ExprNodeBinOp::RANGE && !e->left && !e->right) {
                                ASSERT_BUG(v.span(), !isSplit, "Multiple `..` in tuple pattern?");
                                isSplit = true;
                                subpats_start = std::move(subpats);
                                continue;
                            }
                        }
                        subpats.push_back(lower(m));
                    }
                    if (isSplit) {
                        pat(AST::Pattern(AST::Pattern::TagTuple(), v.span(), AST::Pattern::TuplePat{std::move(subpats_start), true, std::move(subpats)}));
                    } else {
                        pat(AST::Pattern(AST::Pattern::TagTuple(), v.span(), std::move(subpats)));
                    }
                }

                // Just emit as if it's a slot, `UnitStruct = Foo` isn't valid
                void visit(::AST::ExprNodeNamedValue& v) override {
                    slot(v);
                }

                void visit(::AST::ExprNodeField& v) override {
                    slot(v);
                }

                void visit(::AST::ExprNodeIndex& v) override {
                    slot(v);
                }

                void visit(::AST::ExprNodeDeref& v) override {
                    slot(v);
                }

                void visit(::AST::ExprNodeCast& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeTypeAnnotation& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeBinOp& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeUniOp& v) override {
                    invalid(v);
                }

                void visit(::AST::ExprNodeMacroDefinition& v) override {
                    invalid(v);
                }
            } v;

            auto pat = v.lower(node.slot);
            if (pat.bindings().size() > 0) {
                assert(pat.bindings().size() == 1); // The above code shouldn't be making double bindings
                assert(!node.slot);
                assert(v.slots.front().second);
                node.slot = std::move(v.slots.front().second);
            } else {
                // Create a block with a `let` and individual assignments
                auto rv = new AST::ExprNodeBlock();
                rv->nodes.push_back({true, AST::ExprNodeP(new AST::ExprNodeLetBinding(std::move(pat), TypeRef(node.span()), std::move(node.mValue)))});
                for (auto& slots : v.slots) {
                    rv->nodes.push_back({true, AST::ExprNodeP(new AST::ExprNodeAssign(AST::ExprNodeAssign::NONE, std::move(slots.second), AST::ExprNodeP(new AST::ExprNodeNamedValue(AST::Path::new_local(std::move(slots.first))))))});
                }
                this->replacement = AST::ExprNodeP(rv);
            }
        }
    }

    void visit(::AST::ExprNodeCallPath& node) override {
        ExpandPath(this->expandState, this->curMod(), node.mPath);
        this->visit_vector(node.mArgs);
    }

    void visit(::AST::ExprNodeCallMethod& node) override {
        ExpandPathParams(this->expandState, this->curMod(), node.method.args());
        this->visit_nodelete(node, node.val);
        this->visit_vector(node.mArgs);
    }

    void visit(::AST::ExprNodeCallObject& node) override {
        this->visit_nodelete(node, node.val);
        this->visit_vector(node.mArgs);
    }

    void visit(::AST::ExprNodeLoop& node) override {
        this->visit_nodelete(node, node.mCode);
    }

    void visit(::AST::ExprNodeFor& node) override {
        ExpandPattern(this->expandState, this->curMod(), node.pattern, false);
        this->visit_nodelete(node, node.mValue);
        this->visit_nodelete(node, node.mCode);

        static const RcString rcstringIntoIter = RcString::newInterned("into_iter");
        static const RcString rcstringNext = RcString::newInterned("next");
        static const RcString rcstringIt = RcString::newInterned("it");
        const auto iteratorHygiene = Ident::Hygiene::newScope();
        auto coreCrate = crate.extCratenameCore;
        auto path_Some = getPath(coreCrate, "option", "Option", "Some");
        auto path_None = getPath(coreCrate, "option", "Option", "None");
        auto path_IntoIterator = getPath(coreCrate, "iter", "IntoIterator");
        auto path_Iterator = getPath(coreCrate, "iter", "Iterator");
        // Desugar into:
        // {
        //     match <_ as ::iter::IntoIterator>::into_iter(`m_cond`) {
        //     mut it => {
        //         `m_label`: loop {
        //             match ::iter::Iterator::next(&mut it) {
        //             Some(`m_pattern`) => `m_code`,
        //             None => break `m_label`,
        //             }
        //         }
        //     }
        // }
        ::std::vector<::AST::ExprNodeMatchArm> arms;
        // - `Some(pattern ) => code`
        arms.push_back(::AST::ExprNodeMatchArm(::makeVec1(::AST::Pattern(::AST::Pattern::TagNamedTuple(), node.span(), path_Some, ::makeVec1(mv$(node.pattern)))), {}, mv$(node.mCode)));
        // - `None => break label`
        arms.push_back(::AST::ExprNodeMatchArm(::makeVec1(::AST::Pattern(::AST::Pattern::TagValue(), node.span(), ::AST::Pattern::Value::make_Named(path_None))), {}, ::AST::ExprNodeP(new ::AST::ExprNodeFlow(::AST::ExprNodeFlow::BREAK, node.label, nullptr))));

        auto nextReceiver = ::AST::ExprNodeP(new ::AST::ExprNodeNamedValue(::AST::Path::newRelative(
            iteratorHygiene,
            ::makeVec1(::AST::PathNode(rcstringIt))
        )));
        auto nextReceiverBorrow = ::AST::ExprNodeP(new ::AST::ExprNodeUniOp(::AST::ExprNodeUniOp::REFMUT, mv$(nextReceiver)));
        auto nextCall = ::AST::ExprNodeP(new ::AST::ExprNodeCallPath(
            ::AST::Path::newUfcsTrait(::TypeRef(node.span()), path_Iterator, {::AST::PathNode(rcstringNext)}),
            ::makeVec1(mv$(nextReceiverBorrow))));
        auto nextMatch = ::AST::ExprNodeP(new ::AST::ExprNodeMatch(mv$(nextCall), mv$(arms)));
        auto loop = ::AST::ExprNodeP(new ::AST::ExprNodeLoop(node.label, mv$(nextMatch)));

        auto intoIterCall = ::AST::ExprNodeP(new ::AST::ExprNodeCallPath(
            ::AST::Path::newUfcsTrait(::TypeRef(node.span()), path_IntoIterator, {::AST::PathNode(rcstringIntoIter)}),
            ::makeVec1(mv$(node.mValue))));
        auto outerMatch = ::AST::ExprNodeP(new ::AST::ExprNodeMatch(
            mv$(intoIterCall),
            ::makeVec1(::AST::ExprNodeMatchArm(
                ::makeVec1(::AST::Pattern(::AST::Pattern::TagBind(), node.span(), Ident(iteratorHygiene, rcstringIt))),
                {},
                mv$(loop)))));

        // rustc wraps the outer match in `DropTemps`: for always yields (), so
        // a block containing the match as a statement provides the same
        // terminating temporary scope without leaking head temporaries into a
        // surrounding initializer or call expression.
        auto block = new ::AST::ExprNodeBlock();
        block->nodes.push_back({true, mv$(outerMatch)});
        replacement.reset(block);
        replacement->set_span(node.span());
    }

    void visit(::AST::ExprNodeWhile& node) override {
        for (auto& cond : node.conditions) {
            if (cond.optPat) {
                ExpandPattern(this->expandState, this->curMod(), *cond.optPat, true);
            }
            this->visit_nodelete(node, cond.value);
        }
        this->visit_nodelete(node, node.mCode);
    }

    void visit(::AST::ExprNodeMatch& node) override {
        this->visit_nodelete(node, node.val);
        for (auto& arm : node.arms) {
            ExpandAttrsCfgAttr(arm.mAttrs);
            ExpandAttrs(expandState, arm.mAttrs, AttrStage::Pre, [&](const Span& sp, const auto& d, const auto& a) {
                d.handle(sp, a, crate, arm);
            });
            if (arm.patterns.size() == 0) {
                continue;
            }
            for (auto& pat : arm.patterns) {
                ExpandPattern(this->expandState, this->curMod(), pat, true);
            }
            for (auto& cond : arm.guard) {
                if (cond.optPat) {
                    ExpandPattern(this->expandState, this->curMod(), *cond.optPat, true);
                }
                this->visit_nodelete(node, cond.value);
            }

            this->visit_nodelete(node, arm.mCode);
            ExpandAttrs(expandState, arm.mAttrs, AttrStage::Post, [&](const Span& sp, const auto& d, const auto& a) {
                d.handle(sp, a, crate, arm);
            });
        }
        // Prune deleted arms
        for (auto it = node.arms.begin(); it != node.arms.end();) {
            if (it->patterns.size() == 0) {
                it = node.arms.erase(it);
            } else {
                ++it;
            }
        }
    }

    void visit(::AST::ExprNodeIf& node) override {
        for (auto& arm : node.arms) {
            for (auto& cond : arm.conditions) {
                if (cond.optPat) {
                    ExpandPattern(this->expandState, this->curMod(), *cond.optPat, true);
                }
                this->visit_nodelete(node, cond.value);
            }
            this->visit_nodelete(node, arm.body);
        }
        this->visit_nodelete(node, node.elseNode);
    }

    void visit(::AST::ExprNodeWildcardPattern& node) override {
    }

    void visit(::AST::ExprNodeInteger& node) override {
    }

    void visit(::AST::ExprNodeFloat& node) override {
    }

    void visit(::AST::ExprNodeBool& node) override {
    }

    void visit(::AST::ExprNodeString& node) override {
    }

    void visit(::AST::ExprNodeByteString& node) override {
    }

    void visit(::AST::ExprNodeCString& node) override {
    }

    void visit(::AST::ExprNodeClosure& node) override {
        auto try_stack = ::std::move(tryStack);
        for (auto& arg : node.mArgs) {
            ExpandPattern(this->expandState, this->curMod(), arg.first, false);
            ExpandType(this->expandState, this->curMod(), arg.second);
        }
        ExpandType(this->expandState, this->curMod(), node.returnType);
        this->visit_nodelete(node, node.mCode);
        tryStack = std::move(try_stack);
    }

    void visit(::AST::ExprNodeStructLiteral& node) override {
        this->visit_nodelete(node, node.baseValue);
        for (auto& val : node.values) {
            ExpandAttrsCfgAttr(val.attrs);
            ExpandAttrs(expandState, val.attrs, AttrStage::Pre, [&](const Span& sp, const auto& d, const auto& a) {
                d.handle(sp, a, crate, val);
            });
            if (!val.value) {
                continue;
            }
            this->visit_nodelete(node, val.value);
            ExpandAttrs(expandState, val.attrs, AttrStage::Post, [&](const Span& sp, const auto& d, const auto& a) {
                d.handle(sp, a, crate, val);
            });
        }
        for (auto it = node.values.begin(); it != node.values.end();) {
            if (it->value) {
                ++it;
            } else {
                it = node.values.erase(it);
            }
        }
    }

    void visit(::AST::ExprNodeStructLiteralPattern& node) override {
        for (auto& val : node.values) {
            ExpandAttrsCfgAttr(val.attrs);
            ExpandAttrs(expandState, val.attrs, AttrStage::Pre, [&](const Span& sp, const auto& d, const auto& a) {
                d.handle(sp, a, crate, val);
            });
            if (!val.value) {
                continue;
            }
            this->visit_nodelete(node, val.value);
            ExpandAttrs(expandState, val.attrs, AttrStage::Post, [&](const Span& sp, const auto& d, const auto& a) {
                d.handle(sp, a, crate, val);
            });
        }
        for (auto it = node.values.begin(); it != node.values.end();) {
            if (it->value) {
                ++it;
            } else {
                it = node.values.erase(it);
            }
        }
    }

    void visit(::AST::ExprNodeArray& node) override {
        this->visit_nodelete(node, node.mSize);
        this->visit_vector(node.values);
    }

    void visit(::AST::ExprNodeTuple& node) override {
        this->visit_vector(node.values);
    }

    void visit(::AST::ExprNodeNamedValue& node) override {
        ExpandPath(this->expandState, this->curMod(), node.mPath);
    }

    void visit(::AST::ExprNodeField& node) override {
        this->visit_nodelete(node, node.obj);
    }

    void visit(::AST::ExprNodeIndex& node) override {
        this->inAssignLhs = false;
        this->visit_nodelete(node, node.obj);
        this->visit_nodelete(node, node.idx);
    }

    void visit(::AST::ExprNodeDeref& node) override {
        this->visit_nodelete(node, node.mValue);
    }

    void visit(::AST::ExprNodeCast& node) override {
        this->visit_nodelete(node, node.mValue);
        ExpandType(this->expandState, this->curMod(), node.mType);
    }

    void visit(::AST::ExprNodeTypeAnnotation& node) override {
        this->visit_nodelete(node, node.mValue);
        ExpandType(this->expandState, this->curMod(), node.mType);
    }

    void visit(::AST::ExprNodeBinOp& node) override {
        this->visit_nodelete(node, node.left);
        this->visit_nodelete(node, node.right);

        if (this->inAssignLhs) {
            return;
        }
        static const RcString rcstringStart = RcString::newInterned("start");
        static const RcString rcstringEnd = RcString::newInterned("end");
        switch (node.mType) {
            case ::AST::ExprNodeBinOp::RANGE: {
                // NOTE: Not language items pre 1.39
                auto coreCrate = crate.extCratenameCore;
                auto path_Range = getPath(coreCrate, "ops", "Range");
                auto path_RangeFrom = getPath(coreCrate, "ops", "RangeFrom");
                auto path_RangeTo = getPath(coreCrate, "ops", "RangeTo");
                auto path_RangeFull = getPath(coreCrate, "ops", "RangeFull");

                ::AST::ExprNodeStructLiteral::t_values values;
                if (node.left && node.right) {
                    values.push_back({{}, rcstringStart, mv$(node.left)});
                    values.push_back({{}, rcstringEnd, mv$(node.right)});
                    replacement.reset(new ::AST::ExprNodeStructLiteral(mv$(path_Range), nullptr, mv$(values)));
                } else if (node.left) {
                    values.push_back({{}, rcstringStart, mv$(node.left)});
                    replacement.reset(new ::AST::ExprNodeStructLiteral(mv$(path_RangeFrom), nullptr, mv$(values)));
                } else if (node.right) {
                    values.push_back({{}, rcstringEnd, mv$(node.right)});
                    replacement.reset(new ::AST::ExprNodeStructLiteral(mv$(path_RangeTo), nullptr, mv$(values)));
                } else {
                    replacement.reset(new ::AST::ExprNodeStructLiteral(mv$(path_RangeFull), nullptr, mv$(values)));
                }
                replacement->set_span(node.span());
                break;
            }
            case ::AST::ExprNodeBinOp::RANGE_INC: {
                // NOTE: Not language items pre 1.54
                auto coreCrate = crate.extCratenameCore;
                auto path_RangeInclusive_NonEmpty = getPath(coreCrate, "ops", "RangeInclusive");
                auto path_RangeToInclusive = getPath(coreCrate, "ops", "RangeToInclusive");

                if (node.left) {
                    ::AST::ExprNodeStructLiteral::t_values values;
                    values.push_back({{}, rcstringStart, mv$(node.left)});
                    values.push_back({{}, rcstringEnd, mv$(node.right)});
                    values.push_back({{}, RcString::newInterned("exhausted"), ::AST::ExprNodeP(new ::AST::ExprNodeBool(false))});
                    replacement.reset(new ::AST::ExprNodeStructLiteral(mv$(path_RangeInclusive_NonEmpty), nullptr, mv$(values)));
                } else {
                    ::AST::ExprNodeStructLiteral::t_values values;
                    values.push_back({{}, rcstringEnd, mv$(node.right)});
                    replacement.reset(new ::AST::ExprNodeStructLiteral(mv$(path_RangeToInclusive), nullptr, mv$(values)));
                }
                replacement->set_span(node.span());
                break;
            }
            default:
                break;
        }
    }

    void visit(::AST::ExprNodeUniOp& node) override {
        this->visit_nodelete(node, node.mValue);
        // - Desugar question mark operator before resolve so it can create names
        if (node.mType == ::AST::ExprNodeUniOp::QMARK) {
            auto coreCrate = crate.extCratenameCore;

            // TODO: Find a way of creating bindings during HIR lower instead (so lang items are available)

            //auto it = crate.m_lang_items.find("try");
            //ASSERT_BUG(node.span(), it != crate.m_lang_items.end(), "Can't find the `try` lang item");
            //auto path_Try = it->second;
            auto pathTry = getPath(coreCrate, "ops", "Try");
            static const RcString rcstringV = RcString::newInterned("v");
            static const RcString rcstringR = RcString::newInterned("r");
            // TryV2
            {
                auto path_Try_branch = ::AST::Path::newUfcsTrait(::TypeRef(node.span()), pathTry, {::AST::PathNode(RcString::newInterned("branch"))});
                // Not a lang item
                auto path_ControlFlow_Continue = getPath(coreCrate, "ops", "ControlFlow", "Continue");
                auto path_ControlFlow_Break = getPath(coreCrate, "ops", "ControlFlow", "Break");
                auto pathFromResidualFromResidual = getPath(coreCrate, "ops", "FromResidual", "from_residual");

                ::std::vector<::AST::ExprNodeMatchArm> arms;
                // `Continue(v) => v,`
                arms.push_back(::AST::ExprNodeMatchArm(::makeVec1(::AST::Pattern(::AST::Pattern::TagNamedTuple(), node.span(), path_ControlFlow_Continue, ::makeVec1(::AST::Pattern(::AST::Pattern::TagBind(), node.span(), rcstringV)))), {}, ::AST::ExprNodeP(new ::AST::ExprNodeNamedValue(::AST::Path(rcstringV)))));
                // `Break(r) => return R::from_residual(r),`
                arms.push_back(
                    ::AST::ExprNodeMatchArm(
                        ::makeVec1(::AST::Pattern(::AST::Pattern::TagNamedTuple(), node.span(), path_ControlFlow_Break, ::makeVec1(::AST::Pattern(::AST::Pattern::TagBind(), node.span(), rcstringR)))),
                        {},
                        ::AST::ExprNodeP(new ::AST::ExprNodeFlow(
                            (tryStack.empty() ? ::AST::ExprNodeFlow::RETURN : ::AST::ExprNodeFlow::BREAK), // NOTE: uses `break 'tryblock` instead of return if in a try block.
                            (tryStack.empty() ? RcString("") : tryStack.back()),
                            ::AST::ExprNodeP(new ::AST::ExprNodeCallPath(::AST::Path(pathFromResidualFromResidual), ::makeVec1(::AST::ExprNodeP(new ::AST::ExprNodeNamedValue(::AST::Path(rcstringR))))))
                        ))
                    )
                );

                replacement.reset(new ::AST::ExprNodeMatch(::AST::ExprNodeP(new AST::ExprNodeCallPath(mv$(path_Try_branch), ::makeVec1(mv$(node.mValue)))), mv$(arms)));
            }
        }
    }

    void visit(::AST::ExprNodeMacroDefinition&) override {
    }
};

void ExpandExpr(const ExpandState& es, ::AST::ExprNodeP& node) {
    TRACE_FUNCTION_F("unique_ptr");
    CExpandExpr visitor{es};
    visitor.visit(node);
}

void ExpandExpr(const ExpandState& es, ::std::shared_ptr<AST::ExprNode>& node) {
    TRACE_FUNCTION_F("shared_ptr");
    CExpandExpr visitor{es};
    node->visit(visitor);
    if (visitor.replacement) {
        node.reset(visitor.replacement.release());
    }
}

void ExpandExpr(const ExpandState& es, AST::Expr& node) {
    TRACE_FUNCTION_F("AST::Expr");
    CExpandExpr visitor{es};
    node.visit_nodes(visitor);
    if (visitor.replacement) {
        node = AST::Expr(mv$(visitor.replacement));
    }
}

void Expand_GenericParams(const ExpandState& es, ::AST::Module& mod, ::AST::GenericParams& params) {
    for (auto& paramDef : params.mParams) {
        TU_MATCH_HDRA( (paramDef), {)
        TU_ARMA(None, e) {
                // Ignore
            }
            TU_ARMA(Lifetime, e) {
            }
            TU_ARMA(Type, ty_def) {
                ExpandType(es, mod, ty_def.getDefault());
            }
            TU_ARMA(Value, val_def) {
                ExpandType(es, mod, val_def.type());
            }
        }
    }
    for (auto& bound : params.bounds) {
        TU_MATCHA((bound), (be), (None, ), (Lifetime, ), (TypeLifetime, ExpandType(es, mod, be.type);), (IsTrait, ExpandType(es, mod, be.type); ExpandPath(es, mod, be.trait);), (MaybeTrait, ExpandType(es, mod, be.type); ExpandPath(es, mod, be.trait);), (NotTrait, ExpandType(es, mod, be.type); ExpandPath(es, mod, be.trait);), (Equality, ExpandType(es, mod, be.type); ExpandType(es, mod, be.replacement);))
    }
}

void ExpandBareExpr(const ::AST::Crate& crate, const AST::Module& mod, ::AST::ExprNodeP& node) {
    ExpandState es{const_cast<AST::Crate&>(crate), LList<const AST::Module*>(nullptr, &mod), ExpandMode::FirstPass};
    ExpandExpr(es, node);
    es.mode = ExpandMode::Final;
    //Expand_Expr(es, node);
}

::AST::ExprNodeP ExpandParseAndExpandExprVal(const ::AST::Crate& crate, const AST::Module& mod, TokenStream& lex) {
    auto sp = lex.pointSpan();
    auto n = ParseExprVal(lex);
    ASSERT_BUG(sp, n, "No expression returned");
    ExpandBareExpr(crate, mod, n);
    return n;
}

void Expand_Function(const ExpandState& es, AST::Module& mod, AST::Function& e) {
    for (size_t i = 0; i < e.args().size(); i++) {
        auto& arg = e.args()[i];
        if (!ExpandAttrsCfgOnly(es, arg.attrs)) {
            e.args().erase(e.args().begin() + i);
            i--;
            continue;
        }
        ExpandPattern(es, mod, arg.pat, false);
        ExpandType(es, mod, arg.ty);
        ExpandAttrs(es, arg.attrs, AttrStage::Post, [&](const Span& sp, const ExpandDecorator& d, const AST::Attribute& a) {
            TODO(sp, "attributes on function arguments - " << a);
        });
    }
    ExpandType(es, mod, e.rettype());
    ExpandExpr(es, e.code());
}

void Expand_Impl(const ExpandState& es, ::AST::Path modpath, ::AST::Module& mod, ::AST::Impl& impl) {
    TRACE_FUNCTION_F(impl.def());
    Expand_GenericParams(es, mod, impl.def().params());

    ExpandType(es, mod, impl.def().type());
    ExpandPath(es, mod, impl.def().trait().ent);

    DEBUG("> Items");
    for (unsigned int idx = 0; idx < impl.items().size(); idx++) {
        auto i = std::move(impl.items()[idx]);
        DEBUG("  - " << i.name << " :: " << i.attrs);

        // TODO: Make a path from the impl definition? Requires having the impl def resolved to be correct
        // - Does it? the namespace is essentially the same. There may be issues with wherever the path is used though
        // TODO: UFCS path, or different method
        AST::AbsolutePath path("", {"", i.name});

        auto attrs = mv$(i.attrs);
        ExpandAttrsCfgAttr(attrs);
        ExpandAttrs(es, attrs, AttrStage::Pre, mod, impl, i.vis, i.name, *i.data);

        TU_MATCH_HDRA( (*i.data), {)
        default:
            BUG(Span(), "Unknown item type in impl block - " << i.data->tag_str());
            TU_ARMA(None, e) {
            }
            TU_ARMA(MacroInv, e) {
                if (e.path().isValid()) {
                    TRACE_FUNCTION_F("Macro invoke " << e.path());
                    // Move out of the module to avoid invalidation if a new macro invocation is added
                    auto miOwned = mv$(e);

                    auto ttl = ExpandMacro(es, mod, miOwned);
                    ASSERT_BUG(miOwned.span(), ttl, "TODO: Unexpanded macro?");

                    if (ttl) {
                        // Re-parse tt
                        while (ttl->lookahead(0) != TOK_EOF) {
                            ParseImplItem(*ttl, impl);
                        }
                        // - Any new macro invocations ends up at the end of the list and handled
                        ASSERT_BUG(miOwned.span(), impl.items().size() > idx, "");

                        *i.data = AST::Item::make_None({});
                    } else {
                        // Move back in (using the index, as the old pointr may be invalid)
                        i.data->as_MacroInv() = mv$(miOwned);
                    }
                }
            }
            TU_ARMA(Function, e) {
                TRACE_FUNCTION_F("fn " << i.name);
                Expand_Function(es, mod, e);
            }
            TU_ARMA(Static, e) {
                TRACE_FUNCTION_F("static " << i.name);
                ExpandExpr(es, e.value());
                ExpandType(es, mod, e.type());
            }
            TU_ARMA(Type, e) {
                TRACE_FUNCTION_F("type " << i.name);
                ExpandType(es, mod, e.type());
            }
        }
        impl.items()[idx] = std::move(i);

        // Run post-expansion decorators and restore attributes
        {
            auto& i = impl.items()[idx];
            ExpandAttrs(es, attrs, AttrStage::Post, mod, impl, i.vis, i.name, *i.data);
            // TODO: How would this be populated? It got moved out?
            if (i.attrs.mItems.size() == 0) {
                i.attrs = mv$(attrs);
            }
        }
    }
}

void Expand_ImplDef(const ExpandState& es, ::AST::Path modpath, ::AST::Module& mod, ::AST::ImplDef& implDef) {
    Expand_GenericParams(es, mod, implDef.params());

    ExpandType(es, mod, implDef.type());
    //Expand_Type(es, mod,  impl_def.trait());
}

void Expand_ExternBlock(const ExpandState& es, ::AST::Module& mod, ::AST::ExternBlock& block) {
    TRACE_FUNCTION_F("ABI=" << block.abi());
    for (size_t idx = 0; idx < block.items().size(); idx++) {
        auto& i = block.items()[idx];
        DEBUG(i.data.tag_str() << " " << mod.path() << "::" << i.name);

        auto path = mod.path() + i.name;

        auto attrs = mv$(i.attrs);
        auto vis = i.vis;

        auto dat = std::move(i.data);
        TU_MATCH_HDRA( (dat), { )
        default:
            BUG(Span(), "Unexpected item type - " << dat.tag_str());
            TU_ARMA(None, e) {
                // Skip: nothing
            }

            TU_ARMA(Type, e) {
                ExpandType(es, mod, e.type());
            }
            TU_ARMA(Function, e) {
                Expand_Function(es, mod, e);
            }
            TU_ARMA(Static, e) {
                ExpandExpr(es, e.value());
                ExpandType(es, mod, e.type());
            }

            TU_ARMA(MacroInv, e) {
                // Move out of the module to avoid invalidation if a new macro invocation is added
                auto miOwned = mv$(e);

                if (!miOwned.is_expanded()) {
                    assert(miOwned.span());
                    TRACE_FUNCTION_F("Macro invoke " << miOwned.path());

                    auto ttl = ExpandMacro(es, mod, miOwned);
                    if (ttl) {
                        // TODO: What if this attribute adds new items? Or if it changes the type?
                        // - AttrStage::Post doesn't
                        ExpandAttrs(es, attrs, AttrStage::Post, path, mod, idx, vis, dat);

                        // Re-parse tt
                        // TODO: All new items should be placed just after this?
                        DEBUG("-- Parsing as extern block items");
                        auto ipos = block.items().begin() + idx;
                        while (!ttl->getTokenIf(TOK_EOF)) {
                            ipos = block.items().insert(ipos + 1, ParseExternBlockItem(*ttl, block.abi()));
                        }

                        miOwned.set_expanded();
                    } else {
                        DEBUG("Deferred macro");
                    }
                }
                dat.as_MacroInv() = mv$(miOwned);
            }
        }

        {
            auto& i = block.items()[idx];
            i.data = std::move(dat);
            i.vis = std::move(vis);
            i.attrs = std::move(attrs);
        }
    }
}

void ExpandMod(const ExpandState& es, ::AST::AbsolutePath modpath, ::AST::Module& mod, unsigned int firstItem) {
    TRACE_FUNCTION_F("modpath = " << modpath << ", first_item=" << firstItem);

    // TODO: Pre-parse all macro_rules invocations into items?

    if (es.mode == ExpandMode::FirstPass) {
        // Import all macros from parent module.
        if (firstItem == 0) {
            for (const auto& mi : mod.macroImports) {
                DEBUG("- Imports '" << mi.path << "'");
            }
            if (es.modstack.prev) {
                for (const auto& mac : es.modstack.prev->item->macroImports) {
                    mod.macroImports.push_back(mac.clone());
                    DEBUG(mod.path() << " + Import import " << mac.name << " = '" << mod.macroImports.back().path << "'");
                }
                for (const auto& mac : es.modstack.prev->item->macros()) {
                    mod.macroImports.push_back(AST::Module::MacroImport{false, mac.name, es.modstack.prev->item->path() + mac.name, &*mac.data});
                    DEBUG(mod.path() << " + Import defined '" << mod.macroImports.back().path << "'");
                }
            }
        }

        // Insert prelude if: Enabled for this module, present for the crate, and this module is not an anon
        if (es.crate.preludePath != AST::Path()) {
            if (mod.insertPrelude && !mod.isAnon()) {
                DEBUG("> Adding custom prelude " << es.crate.preludePath);
                mod.addItem(Span(), AST::Visibility::makeRestricted(AST::Visibility::Ty::Private, mod.path()), "", ::AST::UseItem{Span(), ::makeVec1(::AST::UseItem::Ent{Span(), es.crate.preludePath, ""})}, {});
            } else {
                DEBUG("> Not inserting custom prelude (anon or disabled)");
            }
        }
    }

    // Stack to prevent macro recursion
    // - Items are popped if the item address matches
    std::vector<const AST::Named<AST::Item>*> macroRecursionStack;

    DEBUG("Items");
    for (unsigned int idx = firstItem; idx < mod.mItems.size(); idx++) {
        auto& i = *mod.mItems[idx];

        // If this is the pop point for this entry, then pop
        // - Note, can be `nullptr`, but that indicates that the macro invocation was the end
        while (!macroRecursionStack.empty() && macroRecursionStack.back() == &i) {
            macroRecursionStack.pop_back();
            DEBUG("End macro recursion guard");
        }

        DEBUG("- " << modpath << "::" << i.name << " (" << ::AST::Item::tag_to_str(i.data.tag()) << ") :: " << i.attrs);
        auto path = modpath + i.name;

        if (const auto* mi = i.data.opt_MacroInv()) {
            if (mi->path().is_trivial() && mi->path().asTrivial() == "macro_rules") {
                i.vis = AST::Visibility::makeGlobal();
                DEBUG("macro_rules made pub");
            }
        }

        // Pre-exapand inner `#[cfg]`
        {
            struct H {
                static void filterCfg(::std::vector<AST::StructItem>& lst) {
                    auto newEnd = ::std::remove_if(lst.begin(), lst.end(), [&](const AST::StructItem& v) {
                        return !checkCfgAttrs(v.mAttrs);
                    });
                    DEBUG(lst.size() << " -> " << newEnd - lst.begin());
                    lst.erase(newEnd, lst.end());
                }

                static void filterCfg(::std::vector<AST::TupleItem>& lst) {
                    auto newEnd = ::std::remove_if(lst.begin(), lst.end(), [&](const AST::TupleItem& v) {
                        return !checkCfgAttrs(v.mAttrs);
                    });
                    DEBUG(lst.size() << " -> " << newEnd - lst.begin());
                    lst.erase(newEnd, lst.end());
                }
            };

            DEBUG(i.data.tag_str() << " " << mod.path() << "::" << i.name);
            TU_MATCH_HDRA( (i.data), { )
            // Expand cfg within types, so derive macros don't need to care
            TU_ARMA(Struct, str) {
                TU_MATCH_HDRA((str.mData), {)
                TU_ARMA(Unit, e) {
                        }
                        TU_ARMA(Struct, e) {
                            H::filterCfg(e.ents);
                        }
                        TU_ARMA(Tuple, e) {
                            H::filterCfg(e.ents);
                        }
                }
                }
                TU_ARMA(Union, unm) {
                    H::filterCfg(unm.mVariants);
                }
                TU_ARMA(Enum, enm) {
                    for (auto it = enm.variants().begin(); it != enm.variants().end();) {
                        if (!checkCfgAttrs(it->mAttrs)) {
                            it = enm.variants().erase(it);
                        } else {
                        TU_MATCH_HDRA( (it->mData), { )
                        TU_ARMA(Unit, e) {
                                }
                                TU_ARMA(Tuple, e) {
                                    H::filterCfg(e.mItems);
                                }
                                TU_ARMA(Struct, e) {
                                    H::filterCfg(e.fields);
                                }
                        }

                        ++ it;
                        }
                    }
                }
                default:
                    break;
            }
        }

        auto attrs = mv$(i.attrs);
        auto vis = i.vis;
        TRACE_FUNCTION_F("#" << idx << " - " << path);
        DEBUG("attrs = " << attrs);
        ExpandAttrsCfgAttr(attrs);
        ExpandAttrs(es, attrs, AttrStage::Pre, path, mod, idx, vis, i.data);

        // Do modules without moving the definition (so the module path is always valid)
        if (i.data.is_Module()) {
            auto& e = i.data.as_Module();
            LList<const AST::Module*> sub_modstack(&es.modstack, &e);
            ExpandState esInner(es.crate, sub_modstack, es.mode);
            ExpandMod(esInner, path, e, 0);
            ExpandAttrs(es, attrs, AttrStage::Post, path, mod, idx, vis, i.data);
            es.change |= esInner.change;
            es.hasMissing |= esInner.hasMissing;
            i.attrs = std::move(attrs);
            continue;
        }

        auto dat = mv$(i.data);

        TU_MATCH_HDRA( (dat), {)
        TU_ARMA(None, e) {
                // Skip: nothing
            }
            TU_ARMA(GlobalAsm, e) {
                // Skip: Nothing to expand
            }
            TU_ARMA(MacroInv, e) {
                // Move out of the module to avoid invalidation if a new macro invocation is added

                if (macroRecursionStack.size() > MAX_MACRO_RECURSION) {
                    ERROR(i.span, E0000, "Exceeded macro recusion limit of " << MAX_MACRO_RECURSION);
                }
                auto miOwned = mv$(e);

                if (!miOwned.is_expanded()) {
                    assert(miOwned.span());
                    TRACE_FUNCTION_F("Macro invoke " << miOwned.path());

                    auto ttl = ExpandMacro(es, mod, miOwned);
                    if (ttl) {
                        ExpandAttrs(es, attrs, AttrStage::Post, path, mod, idx, vis, dat);

                        // Parse
                        DEBUG("-- Parsing as mod items");
                        size_t oldLen = mod.mItems.size();
                        ParseModRootItemsInto(mod, idx, *ttl);

                        auto nextNonMacroItem = idx + 1 + (mod.mItems.size() - oldLen);
                        macroRecursionStack.push_back(nextNonMacroItem == mod.mItems.size() ? nullptr : &*mod.mItems[nextNonMacroItem]);

                        miOwned.set_expanded();
                    } else {
                        DEBUG("Deferred macro");
                    }
                }
                dat.as_MacroInv() = mv$(miOwned);
            }
            TU_ARMA(Macro, e) {
                ASSERT_BUG(i.span, e, "Null macro - " << i.name);
                mod.addMacro(i.vis.isGlobal(), i.name, mv$(e));
                dat = AST::Item::make_None({});
            }
            TU_ARMA(Use, e) {
                // Determine if the `use` refers to a macro, and import into the current scope
                for (const auto& ue : e.entries) {
                    // Get module ref, if it's to a HIR module then grab the macro
                    if (ue.name != "" && ue.path.nodes().size() >= 1) {
                        DEBUG("Use " << ue.path);

                        AST::AbsolutePath refPath;
                        auto m = ResolveLookupMacro(ue.sp, es.crate, mod.path(), ue.path, /*out_path=*/&refPath);
                        MacroRef ref;
                    TU_MATCH_HDRA( (m), { )
                    TU_ARMA(None, e) {
                                // Not found? Ignore.
                            }
                            TU_ARMA(InternalMacro, e) {
                                // Ignore builtins, they're always available.
                            }
                            TU_ARMA(ProcMacro, pm) {
                                ref = pm;
                            }
                            TU_ARMA(MacroRules, mr) {
                                ref = mr;
                            }
                    }
                    if( ! ref.is_None() ) {
                            DEBUG(mod.path() << " + Macro Import: " << refPath);
                            mod.macroImports.push_back(AST::Module::MacroImport{false, ue.name, std::move(refPath), std::move(ref)});
                    }
                    }
                }
            }
            TU_ARMA(ExternBlock, e) {
                Expand_ExternBlock(es, mod, e);
                // HACK: Just convert inner items into outer items
                auto items = mv$(e.items());
                for (auto& i2 : items) {
                    mod.mItems.push_back(box$(i2));
                }
            }
            TU_ARMA(Impl, e) {
                Expand_Impl(es, modpath, mod, e);
            }
            TU_ARMA(NegImpl, e) {
                Expand_ImplDef(es, modpath, mod, e);
            }
            TU_ARMA(Module, e) {
                throw "";
            }
            TU_ARMA(Crate, e) {
                if (e.name != "") {
                    // Can't recurse into an `extern crate`
                    if (es.crate.externCrates.count(e.name) == 0) {
                        e.name = es.crate.loadExternCrate(i.span, e.name);
                    }
                    // Crates imported in root are added to the implicit list
                    if (modpath.nodes.empty()) {
                        AST::gImplicitCrates.insert(std::make_pair(i.name, e.name));
                    }
                } else {
                    if (modpath.nodes.empty()) {
                        AST::gImplicitCrates.insert(std::make_pair(i.name, ""));
                    }
                }
            }

            TU_ARMA(Struct, e) {
                Expand_GenericParams(es, mod, e.params());
            TU_MATCH_HDRA( (e.mData), {)
            TU_ARMA(Unit, sd) {
                    }
                    TU_ARMA(Struct, sd) {
                        for (auto it = sd.ents.begin(); it != sd.ents.end();) {
                            auto& si = *it;
                            ExpandAttrsCfgAttr(si.mAttrs);
                            ExpandAttrs(es, si.mAttrs, AttrStage::Pre, [&](const Span& sp, const auto& d, const auto& a) {
                                d.handle(sp, a, es.crate, si);
                            });
                            ExpandType(es, mod, si.mType);
                            ExpandExpr(es, si.defaultValue);
                            ExpandAttrs(es, si.mAttrs, AttrStage::Post, [&](const Span& sp, const auto& d, const auto& a) {
                                d.handle(sp, a, es.crate, si);
                            });

                            if (si.mName == "") {
                                it = sd.ents.erase(it);
                            } else {
                                ++it;
                            }
                        }
                    }
                    TU_ARMA(Tuple, sd) {
                        for (auto it = sd.ents.begin(); it != sd.ents.end();) {
                            auto& si = *it;
                            ExpandAttrsCfgAttr(si.mAttrs);
                            ExpandAttrs(es, si.mAttrs, AttrStage::Pre, [&](const Span& sp, const auto& d, const auto& a) {
                                d.handle(sp, a, es.crate, si);
                            });
                            ExpandType(es, mod, si.mType);
                            ExpandAttrs(es, si.mAttrs, AttrStage::Post, [&](const Span& sp, const auto& d, const auto& a) {
                                d.handle(sp, a, es.crate, si);
                            });

                            if (!si.mType.isValid()) {
                                it = sd.ents.erase(it);
                            } else {
                                ++it;
                            }
                        }
                    }
            }
            }
            TU_ARMA(Enum, e) {
                Expand_GenericParams(es, mod, e.params());
                for (auto& var : e.variants()) {
                    ExpandAttrsCfgAttr(var.mAttrs);
                    ExpandAttrs(es, var.mAttrs, AttrStage::Pre, [&](const Span& sp, const auto& d, const auto& a) {
                        d.handle(sp, a, es.crate, var);
                    });
                TU_MATCH_HDRA( (var.mData), {)
                TU_ARMA(Unit, e) {
                        }
                        TU_ARMA(Tuple, e) {
                            for (auto it = e.mItems.begin(); it != e.mItems.end();) {
                                auto& si = *it;
                                ExpandAttrsCfgAttr(si.mAttrs);
                                ExpandAttrs(es, si.mAttrs, AttrStage::Pre, [&](const Span& sp, const auto& d, const auto& a) {
                                    d.handle(sp, a, es.crate, si);
                                });
                                ExpandType(es, mod, si.mType);
                                ExpandAttrs(es, si.mAttrs, AttrStage::Pre, [&](const Span& sp, const auto& d, const auto& a) {
                                    d.handle(sp, a, es.crate, si);
                                });
                                if (!si.mType.isValid()) {
                                    it = e.mItems.erase(it);
                                } else {
                                    ++it;
                                }
                            }
                        }
                        TU_ARMA(Struct, e) {
                            for (auto it = e.fields.begin(); it != e.fields.end();) {
                                auto& si = *it;
                                ExpandAttrsCfgAttr(si.mAttrs);
                                ExpandAttrs(es, si.mAttrs, AttrStage::Pre, [&](const Span& sp, const auto& d, const auto& a) {
                                    d.handle(sp, a, es.crate, si);
                                });
                                ExpandType(es, mod, si.mType);
                                ExpandExpr(es, si.defaultValue);
                                ExpandAttrs(es, si.mAttrs, AttrStage::Post, [&](const Span& sp, const auto& d, const auto& a) {
                                    d.handle(sp, a, es.crate, si);
                                });

                                if (si.mName == "") {
                                    it = e.fields.erase(it);
                                } else {
                                    ++it;
                                }
                            }
                        }
                }
                ExpandExpr(es,  var.discriminantValue);
                ExpandAttrs(es, var.mAttrs, AttrStage::Post,  [&](const Span& sp, const auto& d, const auto& a){
                        d.handle(sp, a, es.crate, var); });
                }
                // Handle cfg on variants (kinda hacky)
                for (auto it = e.variants().begin(); it != e.variants().end();) {
                    if (it->mName == "") {
                        it = e.variants().erase(it);
                    } else {
                        ++it;
                    }
                }
            }
            TU_ARMA(Union, e) {
                Expand_GenericParams(es, mod, e.mParams);
                for (auto it = e.mVariants.begin(); it != e.mVariants.end();) {
                    auto& si = *it;
                    ExpandAttrsCfgAttr(si.mAttrs);
                    ExpandAttrs(es, si.mAttrs, AttrStage::Pre, [&](const Span& sp, const auto& d, const auto& a) {
                        d.handle(sp, a, es.crate, si);
                    });
                    ExpandType(es, mod, si.mType);
                    ExpandExpr(es, si.defaultValue);
                    ExpandAttrs(es, si.mAttrs, AttrStage::Post, [&](const Span& sp, const auto& d, const auto& a) {
                        d.handle(sp, a, es.crate, si);
                    });

                    if (si.mName == "") {
                        it = e.mVariants.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
            TU_ARMA(Trait, e) {
                Expand_GenericParams(es, mod, e.params());
                for (auto& p : e.supertraits()) {
                    ExpandPath(es, mod, *p.ent.path);
                }
                auto& trait_items = e.items();
                for (size_t idx = 0; idx < trait_items.size(); idx++) {
                    auto& ti = trait_items[idx];
                    DEBUG(" - " << ti.name << " " << ti.data.tag_str());
                    auto attrs = mv$(ti.attrs);
                    auto ti_path = path + ti.name;
                    ExpandAttrsCfgAttr(attrs);
                    ExpandAttrs(es, attrs, AttrStage::Pre, ti_path, mod, e, ti.data);

                TU_MATCH_HDRA( (ti.data), {)
                default:
                    BUG(Span(), "Unknown item type in trait block - " << ti.data.tag_str());
                        TU_ARMA(None, e) {
                        }
                        TU_ARMA(MacroInv, e) {
                            if (e.path().isValid()) {
                                TRACE_FUNCTION_F("Macro invoke " << e.path());
                                // Move out of the module to avoid invalidation if a new macro invocation is added
                                auto miOwned = mv$(e);

                                auto ttl = ExpandMacro(es, mod, miOwned);
                                ASSERT_BUG(miOwned.span(), ttl, "TODO: Unexpanded macro");

                                if (ttl.get()) {
                                    // Re-parse tt
                                    size_t insertPos = idx + 1;
                                    while (ttl->lookahead(0) != TOK_EOF) {
                                        auto i = ParseTraitItem(*ttl);
                                        trait_items.insert(trait_items.begin() + insertPos, mv$(i));
                                        insertPos++;
                                    }
                                    // - Any new macro invocations ends up at the end of the list and handled
                                    trait_items[idx].data = AST::Item::make_None({});
                                } else {
                                    // Move back in (using the index, as the old pointer may be invalid)
                                    trait_items[idx].data.as_MacroInv() = mv$(miOwned);
                                }
                            }
                        }
                        TU_ARMA(Function, e) {
                            Expand_Function(es, mod, e);
                        }
                        TU_ARMA(Static, e) {
                            ExpandExpr(es, e.value());
                            ExpandType(es, mod, e.type());
                        }
                        TU_ARMA(Type, e) {
                            ExpandType(es, mod, e.type());
                        }
                }

                {
                        auto& ti = trait_items[idx];

                        ExpandAttrs(es, attrs, AttrStage::Post, ti_path, mod, e, ti.data);
                        if (ti.attrs.mItems.size() == 0) {
                            ti.attrs = mv$(attrs);
                        }
                }
                }
            }
            TU_ARMA(Type, e) {
                ExpandType(es, mod, e.type());
            }

            TU_ARMA(Function, e) {
                Expand_Function(es, mod, e);
            }
            TU_ARMA(Static, e) {
                ExpandExpr(es, e.value());
                ExpandType(es, mod, e.type());
            }
            TU_ARMA(TraitAlias, e) {
                for (auto& p : e.traits) {
                    ExpandPath(es, mod, *p.ent.path);
                }
            }
        }
        ExpandAttrs(es, attrs, AttrStage::Post,  path, mod, idx, vis, dat);

        {
            auto& i = *mod.mItems[idx];
            if (i.data.tag() == ::AST::Item::TAGDEAD) {
                i.data = mv$(dat);
            }
            // TODO: When would this _not_ be empty?
            if (i.attrs.mItems.size() == 0) {
                i.attrs = mv$(attrs);
            }
        }
    }

    // IGNORE m_anon_modules, handled as part of expressions

    //for( const auto& mi: mod.macro_imports_res() )
    //    DEBUG("- Imports '" << mi.name << "'");
}

void Expand_Mod_IndexAnon(::AST::Crate& crate, ::AST::Module& mod) {
    TRACE_FUNCTION_F("mod=" << mod.path());

    for (auto& i : mod.mItems) {
        DEBUG("- " << i->data.tag_str() << " '" << i->name << "'");
        if (auto* e = i->data.opt_Module()) {
            Expand_Mod_IndexAnon(crate, *e);

            // TODO: Also ensure that all #[macro_export] macros end up in parent
        }
    }

    for (auto& mp : mod.anonMods()) {
        if (mp.use_count() == 1) {
            DEBUG("- " << mp->path() << " dropped due to node destruction");
            mp.reset();
        } else {
            Expand_Mod_IndexAnon(crate, *mp);
        }
    }
}

//
// Expand all `cfg` attributes... mostly to find #[macro_export]
//
void Expand_Mod_Early(::AST::Crate& crate, ::AST::Module& mod, std::vector<std::unique_ptr<AST::Named<AST::Item>>>& newRootItems) {
    TRACE_FUNCTION_F(mod.path());
    for (auto& i : mod.mItems) {
        if (const auto* mi = i->data.opt_MacroInv()) {
            if (mi->path().is_trivial() && mi->path().asTrivial() == "macro_rules") {
                i->vis = AST::Visibility::makeGlobal();
                DEBUG("macro_rules made pub");
            }
        }

        ExpandAttrsCfgAttr(i->attrs);
        bool isMacroExport = false;
        bool cfgFailed = false;
        for (auto& a : i->attrs.mItems) {
            if (a.name() == "cfg") {
                if (!checkCfg(i->span, a)) {
                    cfgFailed = true;
                }
            } else if (a.name() == "macro_export") {
                isMacroExport = true;
            } else {
            }
        }
        if (cfgFailed) {
            i->data = ::AST::Item::make_None({});
        } else if (isMacroExport) {
            if (i->data.is_MacroInv() && i->data.as_MacroInv().path().is_trivial() && i->data.as_MacroInv().path().asTrivial() == "macro_rules") {
                const auto& macInv = i->data.as_MacroInv();
                DEBUG("macro_rules marked with #[macro_export] moved to the crate root - " << macInv.inputIdent());
                newRootItems.push_back(box$(*i));
                i->data = AST::Item();

            } else if (i->data.is_Macro()) {
                // TODO: `#[macro_export] macro foo { ... }` DOESN'T move the item to the root
                // - Instead, it should add an alias? Or just tag for export
                DEBUG("macro item export: " << i->name);
                i->data.as_Macro()->exported = true;
            } else {
                ERROR(i->span, E0000, "#[macro_export] on non-macro_rules - " << i->data.tag_str());
            }
        } else if (auto* e = i->data.opt_Module()) {
            Expand_Mod_Early(crate, *e, newRootItems);
        } else {
        }
    }

    DEBUG("Items");
    for (unsigned int idx = 0; idx < mod.mItems.size(); idx++) {
        auto& i = *mod.mItems[idx];
        if (auto* mi = i.data.opt_MacroInv()) {
            // 1.74 HACK - Parse `macro_rules` during the first pass, so they're present for `use` to refer to
            if (mi->path().is_trivial() && mi->path().asTrivial() == "macro_rules") {
                // A #[rustc_builtin_macro] declaration only supplies the
                // public signature of a compiler-provided macro.  Its
                // macro_rules body is a placeholder and must never enter the
                // ordinary macro namespace or shadow the builtin expander.
                if (i.attrs.get("rustc_builtin_macro")) {
                    continue;
                }
                auto miOwned = mv$(*mi);

                TRACE_FUNCTION_F("Macro invoke " << miOwned.path());

                ExpandState es{crate, {}, ExpandMode::Iterate};
                auto ttl = ExpandMacro(es, mod, miOwned);
                ASSERT_BUG(miOwned.span(), ttl, "BUG: macro_rules not expanded");
                assert(miOwned.path().isValid());

                if (ttl.get()) {
                    // Re-parse tt
                    assert(ttl.get());
                    DEBUG("-- Parsing as mod items");
                    ParseModRootItemsInto(mod, idx, *ttl);

                    //auto next_non_macro_item = idx + 1 + new_item_count;
                    //macro_recursion_stack.push_back(next_non_macro_item == mod.m_items.size() ? nullptr : &*mod.m_items[next_non_macro_item]);
                    //mod.m_items[idx]->data = AST::Item::make_None({});
                } else {
                }
                mod.mItems[idx]->data.as_MacroInv() = mv$(miOwned);
            }
        }
    }
}

void Expand(::AST::Crate& crate) {
    for (const auto& e : gDecorators) {
        DEBUG("Decorator: " << e.first);
    }
    for (const auto& e : gMacros) {
        DEBUG("Macro: " << e.first);
    }

    ExpandState es{crate, LList<const ::AST::Module*>(nullptr, &crate.rootModule), ExpandMode::FirstPass};

    // 1. Crate attributes
    ExpandAttrsCfgAttr(crate.mAttrs);
    ExpandAttrs(es, crate.mAttrs, AttrStage::Pre, [&](const Span& sp, const auto& d, const auto& a) {
        d.handle(sp, a, crate);
    });

    // TODO: Crate name and type

    std::vector<std::unique_ptr<AST::Named<AST::Item>>> newRootItems;
    Expand_Mod_Early(crate, crate.rootModule, newRootItems);
    crate.rootModule.mItems.insert(crate.rootModule.mItems.begin(), std::make_move_iterator(newRootItems.begin()), std::make_move_iterator(newRootItems.end()));

    // Insert magic for libstd/libcore
    // NOTE: The actual crates are loaded in "LoadCrates" using magic in AST::Crate::load_externs
    RcString std_crate_shortname;
    RcString std_crate_name;
    switch (crate.loadStd) {
        case ::AST::Crate::LOAD_STD:
            std_crate_shortname = RcString::newInterned("std");
            std_crate_name = crate.extCratenameStd;
            break;
        case ::AST::Crate::LOAD_CORE:
            std_crate_shortname = RcString::newInterned("core");
            std_crate_name = crate.extCratenameCore;
            break;
        case ::AST::Crate::LOAD_NONE:
            break;
    }
    if (std_crate_shortname != "") {
        ASSERT_BUG(Span(), std_crate_name != "", "`" << std_crate_shortname << "` not loaded?");
        if (crate.preludePath == AST::Path()) {
            switch (crate.edition) {
                case ::AST::Edition::Rust2015:
                case ::AST::Edition::Rust2018:
                    crate.preludePath = AST::Path(std_crate_name, {AST::PathNode("prelude"), AST::PathNode("v1")});
                    break;
                case ::AST::Edition::Rust2021:
                    crate.preludePath = AST::Path(std_crate_name, {AST::PathNode("prelude"), AST::PathNode("rust_2021")});
                    break;
                case ::AST::Edition::Rust2024:
                    crate.preludePath = AST::Path(std_crate_name, {AST::PathNode("prelude"), AST::PathNode("rust_2024")});
                    break;
            }
        }
        AST::AttributeList attrs;
        AST::AttributeName name;
        name.elems.push_back("macro_use");
        attrs.push_back(AST::Attribute(Span(), mv$(name), {}));
        // NOTE: For `macro_use` we want to import this first, but for item lookup, we want it to be last.
        // - Solution, add to the end - but pre-visit the attributes
        crate.rootModule.mItems.push_back(box$(AST::Named<AST::Item>(Span(), mv$(attrs), AST::Visibility::makeRestricted(AST::Visibility::Ty::Private, AST::AbsolutePath()), std_crate_shortname, AST::Item::make_Crate({std_crate_name}))));
        auto& i = *crate.rootModule.mItems.back();
        ExpandAttrs(es, i.attrs, AttrStage::Post, ::AST::AbsolutePath(), crate.rootModule, 0, i.vis, i.data);
    }

    // 2. Module attributes
    for (auto& a : crate.mAttrs.mItems) {
        for (auto& d : gDecorators) {
            if (a.name() == d.first && d.second->stage() == AttrStage::Pre) {
                //d.second->handle(a, crate, ::AST::Path(), crate.m_root_module, crate.m_root_module);
            }
        }
    }

    // 3. Module tree
    // Loop until no more expansions happen
    // - Combine this with allowing macros to fail to expand, to be caught with a final pass
    ExpandMod(es, ::AST::AbsolutePath(), crate.rootModule);
    DEBUG("(first) es.change = " << es.change << ", es.has_missing=" << es.hasMissing << " (" << &es << ")");
    if (es.hasMissing) {
        for (size_t nIters = 0; nIters < 5 && es.change && es.hasMissing; nIters++) {
            es.mode = ExpandMode::Iterate;
            es.change = false;
            es.hasMissing = false;
            ExpandMod(es, ::AST::AbsolutePath(), crate.rootModule);
            DEBUG("?(Iter) es.change = " << es.change << ", es.has_missing=" << es.hasMissing);
        }
        //ASSERT_BUG(Span(), !es.has_missing, "Expand too too many attempts");
        es.hasMissing = false;
    }
    es.mode = ExpandMode::Final;
    ExpandMod(es, ::AST::AbsolutePath(), crate.rootModule);
    ASSERT_BUG(Span(), !es.hasMissing, "Expand too too many attempts");

    //Expand_Attrs(es, crate.m_attrs, AttrStage::Post,  [&](const Span& sp, const auto& d, const auto& a){ d.handle(sp, a, crate); });

    // Post-process
    Expand_Mod_IndexAnon(crate, crate.rootModule);

    // Extract exported macros

    {
        auto& exported_macros = crate.exportedMacros;

        ::std::vector<::AST::Module*> mods;
        mods.push_back(&crate.rootModule);
        do {
            auto& mod = *mods.back();
            mods.pop_back();

            for (/*const*/ auto& mac : mod.macros()) {
                if (mac.data->exported) {
                    auto res = exported_macros.insert(::std::make_pair(mac.name, &*mac.data));
                    if (res.second) {
                        DEBUG("- Define " << mac.name << "!");
                    }
                } else {
                    DEBUG("- Non-exported " << mac.name << "!");
                }
            }

            for (auto& i : mod.mItems) {
                if (i->data.is_Module()) {
                    mods.push_back(&i->data.as_Module());
                }
            }
        } while (mods.size() > 0);

        // - Exported macros imported by the root (is this needed?)
        // - Re-exported macros (ignore proc macros for now?)
        for (const auto& mac : crate.rootModule.macroImports) {
            if (mac.isPub) {
                if (!mac.ref.is_MacroRules()) {
                    continue;
                }
                auto v = ::std::make_pair(mac.name, mac.ref.as_MacroRules());

                auto it = exported_macros.find(mac.name);
                if (it == exported_macros.end()) {
                    auto res = exported_macros.insert(mv$(v));
                    DEBUG("- Import " << mac.name << "! (from \"" << res.first->second->sourceCrate << "\")");
                } else if (v.second->rules.empty()) {
                    // Skip
                } else {
                    DEBUG("- Replace " << mac.name << "! (from \"" << it->second->sourceCrate << "\") with one from \"" << v.second->sourceCrate << "\"");
                    it->second = mv$(v.second);
                }
            }
        }
    }
}
