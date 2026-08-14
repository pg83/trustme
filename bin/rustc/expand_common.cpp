#include "expand_common.h"
#include "expand_common.h"

#include "synext.h"
#include "ast_ast.h"
#include "hir_hir.h" // For macro lookup
#include "ast_expr.h"
#include "settings.h"
#include "ast_crate.h"
#include "expand_cfg.h"
#include "wire_board.h"
#include "parse_common.h" // For reparse from macros
#include "main_bindings.h"
#include "parse_ttstream.h"
#include "resolve_common.h"
#include "expand_proc_macro.h"
#include "macro_rules_macro_rules.h"

#include <map>

// TODO: Respect the crate attribute #![recursion_limit]
#define MAX_MACRO_RECURSION 256

DecoratorDef* gDecoratorsList = nullptr;
MacroDef* gMacrosList = nullptr;
::std::map<RcString, ::std::unique_ptr<ExpandDecorator>> gDecorators;
::std::map<RcString, ::std::unique_ptr<ExpandProcMacro>> gMacros;
// HACK: Used for expanding proc macros, which need to re-parse without access to the current module
// - Parsing needs module for 1) anon modules, and 2) expanding `#[path]`
ASTModule* gCurrentMod = nullptr;

enum class ExpandMode {
    FirstPass,
    Iterate,
    Final,
};

struct ExpandState {
    const WireBoard& wb;
    ASTCrate& crate;
    LList<const ASTModule*> modstack;
    ExpandMode mode;
    mutable bool change;
    mutable bool hasMissing;

    ExpandState(const WireBoard& wb, ASTCrate& crate, LList<const ASTModule*> modstack, ExpandMode mode)
        : wb(wb)
        , crate(crate)
        , modstack(modstack)
        , mode(mode)
        , change(false)
        , hasMissing(false)
    {
        DEBUG("" << this);
    }

    explicit ExpandState(const ExpandState&) = default;
};

void ExpandAttrs(const ExpandState& es, const ASTAttributeList& attrs, AttrStage stage, ::std::function<void(const ExpandDecorator& d, const ASTAttribute& a)> f);
void ExpandMod(const ExpandState& es, ASTAbsolutePath modpath, ASTModule& mod, unsigned int firstItem = 0);
void ExpandExpr(const ExpandState& es, ASTExprNodeP& node);
void ExpandExpr(const ExpandState& es, ASTExpr& node);
void ExpandExpr(const ExpandState& es, ::std::shared_ptr<ASTExprNode>& node);
void ExpandPath(const ExpandState& es, ASTModule& mod, ASTPath& p);
void ExpandPathParams(const ExpandState& es, ASTModule& mod, ASTPathParams& params);

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

void ExpandDecorator::unexpected(const Span& sp, const ASTAttribute& mi, const char* locStr) const {
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

void ParseModRootItemsInto(ASTModule& mod, size_t idx, TokenStream& lex) {
    // Move the item list out
    auto oldItems = std::move(mod.mItems);
    // Parse module items
    ParseModRootItems(lex, mod);
    // Then insert the newly created items
    oldItems.insert(oldItems.begin() + idx + 1, std::make_move_iterator(mod.mItems.begin()), std::make_move_iterator(mod.mItems.end()));
    // and move the (updated) item list back in
    mod.mItems = std::move(oldItems);
}

void ExpandAttr(const ExpandState& es, const Span& sp, const ASTAttribute& a, AttrStage stage, ::std::function<void(const Span& sp, const ExpandDecorator& d, const ASTAttribute& a)> f) {
    bool found = false;
    if (a.name().elems.empty()) {
        return;
    }
    if (a.isInert()) {
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
        auto m = ExpandLookupMacro(sp, es.wb, es.crate, es.modstack, a.name());
        DEBUG(a.name() << " : " << m.tagStr());
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
                void handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t modIdx, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
                    if (!i.is_None()) {
                        auto lex = ProcMacroInvoke(sp, wb, crate, this->macPath, attr.data(), attrs, vis, path.nodes.back(), i);
                        if (lex) {
                            // TODO: `derive_where` returns its own attribute invocation in the output, between two other additions
                            //   > This seems to so the derive (first attribute) can see the trait list? (does mrustc handle that properly? I think so)
                            // - Could parse, and the locate the first matching item (same name?) and merge/filter its attributes
                            // - Should the rest of the attributes be applied
                            // - Tag the item (or item-range?) to stop it being able to invoke this macro again?
                            //  > OR, Should the derive macro be consuming the attribute?
                            // QUERY: Is it valid for there to be multiple items generated from an attribute proc macro?

                            // NOTE: The rust book isn't very clear on the details of how attribute macros work (especially with other/previous attributes)

                            i = ASTItem::make_None({});
                            lex->parseState().module = &mod;
                            ParseModRootItemsInto(mod, modIdx, *lex);
                        } else {
                            ERROR(sp, E0000, "proc_macro expansion failed");
                        }
                    }
                }

                // Impl item
                void handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
                    if (!i.is_None()) {
                        auto lex = ProcMacroInvoke(sp, wb, crate, this->macPath, attr.data(), attrs, vis, name, i);
                        if (lex) {
                            i = ASTItem::make_None({});
                            assert(gCurrentMod);
                            lex->parseState().module = gCurrentMod;
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
                d.macPath.push_back(procMac->path.crateName());
                d.macPath.insert(d.macPath.end(), procMac->path.components().begin(), procMac->path.components().end());
                f(sp, d, a);
                a.markInert();
            }
            found = true;
        } else if (m.is_MacroRules()) {
            // Ignore
        } else {
            TODO(sp, "Attr " << a.name() << " : " << m.tagStr());
        }
    }
    if (!found) {
        // TODO: Create no-op handlers for a whole heap of attributes
        // - There's a LOT
        //TODO(sp, "Unknown attribute #[" << a.name() << "]");
    }
}

void ExpandAttrs(const ExpandState& es, const ASTAttributeList& attrs, AttrStage stage, ::std::function<void(const Span& sp, const ExpandDecorator& d, const ASTAttribute& a)> f) {
    // Reduce load on derive etc by visiting `cfg` first.
    for (auto& a : attrs.mItems) {
        static const RcString rcstringCfg = RcString::newInterned("cfg");
        if (!a.isInert() && a.name() == rcstringCfg) {
            ExpandAttr(es, a.span(), a, stage, f);
        }
    }
    for (auto& a : attrs.mItems) {
        ExpandAttr(es, a.span(), a, stage, f);
    }
}

void ExpandAttrsCfgAttr(const Settings& settings, ASTAttributeList& attrs) {
    for (auto it = attrs.mItems.begin(); it != attrs.mItems.end();) {
        auto& a = *it;
        static const RcString rcstringCfgAttr = RcString::newInterned("cfg_attr");
        if (a.name() == rcstringCfgAttr) {
            auto newAttrs = checkCfgAttr(settings, a);
            it = attrs.mItems.erase(it);
            it = attrs.mItems.insert(it, std::make_move_iterator(newAttrs.begin()), std::make_move_iterator(newAttrs.end()));
        } else {
            ++it;
        }
    }
}

namespace {
    slice<const ASTAttribute> getAttrsAfter(const ASTAttributeList& attrs, const ASTAttribute& a) {
        const auto* start = &a + 1;
        const auto* end = &attrs.mItems.back() + 1;
        return slice<const ASTAttribute>(start, end - start);
    }
}

void ExpandAttrs(const ExpandState& es, const ASTAttributeList& attrs, AttrStage stage, const ASTAbsolutePath& path, ASTModule& mod, size_t modIdx, const ASTVisibility& vis, ASTItem& item) {
    ExpandAttrs(es, attrs, stage, [&](const Span& sp, const ExpandDecorator& d, const ASTAttribute& a) {
        if (!item.is_None()) {
            // Pass attributes _after_ this attribute (or all of them, if the decorator asks)
            auto attrsSlice = d.wantsAllAttrs() ? slice<const ASTAttribute>(attrs.mItems.data(), attrs.mItems.size()) : getAttrsAfter(attrs, a);
            d.handle(sp, a, es.wb, es.crate, path, mod, modIdx, attrsSlice, vis, item);
        }
    });
}

void ExpandAttrs(const ExpandState& es, const ASTAttributeList& attrs, AttrStage stage, const ASTAbsolutePath& path, ASTModule& mod, ASTTrait& trait, ASTItem& item) {
    gCurrentMod = &mod;
    ExpandAttrs(es, attrs, stage, [&](const Span& sp, const auto& d, const ASTAttribute& a) {
        if (!item.is_None()) {
            d.handle(sp, a, es.wb, es.crate, path, trait, getAttrsAfter(attrs, a), item);
        }
    });
    gCurrentMod = nullptr;
}

void ExpandAttrs(const ExpandState& es, const ASTAttributeList& attrs, AttrStage stage, ASTModule& mod, ASTImpl& impl, const ASTVisibility& vis, const RcString& name, ASTItem& item) {
    gCurrentMod = &mod;
    ExpandAttrs(es, attrs, stage, [&](const Span& sp, const auto& d, const auto& a) {
        if (!item.is_None()) {
            d.handle(sp, a, es.wb, es.crate, impl, name, getAttrsAfter(attrs, a), vis, item);
        }
    });
    gCurrentMod = nullptr;
}

bool ExpandAttrsCfgOnly(const ExpandState& es, ASTAttributeList& attrs) {
    bool remove = false;
    ExpandAttrsCfgAttr(*es.wb.settings, attrs);
    ExpandAttrs(es, attrs, AttrStage::Pre, [&](const Span& sp, const ExpandDecorator& d, const ASTAttribute& a) {
        if (a.name() == "cfg") {
            if (!checkCfg(*es.wb.settings, sp, a)) {
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

MacroRef ExpandLookupMacro(const Span& miSpan, const WireBoard& wb, const ASTCrate& crate, LList<const ASTModule*> modstack, const ASTAttributeName& path) {
    ASTPath p = ASTPath::newRelative({}, {});
    for (const auto& ent : path.elems) {
        p += ASTPathNode(ent);
    }
    return ExpandLookupMacro(miSpan, wb, crate, modstack, p);
}

MacroRef ExpandLookupMacro(const Span& miSpan, const WireBoard& wb, const ASTCrate& crate, LList<const ASTModule*> modstack, const ASTPath& path) {
    ASSERT_BUG(miSpan, path.size() > 0, "Path should have nodes: " << path);

    if (path.isTrivial()) {
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
    if (path.isAbsolute() && path.cls.as_Absolute().crate == "" && path.nodes().size() == 1) {
        const auto& name = path.nodes()[0].name();
        if (auto* pm = ExpandFindProcMacro(name)) {
            return MacroRef(pm);
        }
    }

    // Resolve the path, following use statements (if required)
    // - Only mr_ptr matters, as proc_mac is about builtins
    auto rv = ResolveLookupMacro(miSpan, *wb.settings, crate, modstack.item->path(), path, /*out_path=*/nullptr);
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

::std::unique_ptr<TokenStream> ExpandMacroInner(const WireBoard& wb, const ASTCrate& crate, LList<const ASTModule*> modstack, ASTModule& mod, Span miSpan, const ASTPath& path, const RcString& inputIdent, TokenTree& inputTt) {
    ASSERT_BUG(miSpan, path.isValid(), "Macro invocation with invalid path");

    TRACE_FUNCTION_F("Searching for macro " << path);

    // Find the macro
    auto mac = ExpandLookupMacro(miSpan, wb, crate, modstack, path);
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
            macPath.push_back(procMac->path.crateName());
            macPath.insert(macPath.end(), procMac->path.components().begin(), procMac->path.components().end());
            rv = ProcMacroInvoke(miSpan, wb, crate, macPath, inputTt);
        }
        TU_ARMA(BuiltinProcMacro, procMac) {
            ASSERT_BUG(miSpan, procMac, "null BuiltinProcMacro? " << path);
            rv = inputIdent == "" ? procMac->expand(miSpan, wb, crate, inputTt, mod) : procMac->expandIdent(miSpan, wb, crate, inputIdent, inputTt, mod);
        }
        TU_ARMA(MacroRules, mrPtr) {
            if (inputIdent != "") {
                ERROR(miSpan, E0000, "macro_rules! macros can't take an ident");
            }

            DEBUG("Invoking macro_rules " << path << " " << mrPtr);
            rv = MacroInvokeRules(path.isTrivial() ? path.asTrivial() : RcString::newInterned(FMT(path).c_str()), *mrPtr, miSpan, wb, mv$(inputTt), crate, mod);
            inputTt = TokenTree();
        }
    }
    ASSERT_BUG(miSpan, rv, "Macro invocation returned null tokentree");
    return rv;
}

::std::unique_ptr<TokenStream> ExpandMacro(const ExpandState& es, ASTModule& mod, Span miSpan, const ASTPath& path, const RcString& inputIdent, TokenTree& inputTt) {
    auto rv = ExpandMacroInner(es.wb, es.crate, es.modstack, mod, miSpan, path, inputIdent, inputTt);
    if (rv) {
        es.change = true;
        DEBUG("Change flagged");
        rv->parseState().crate = &es.crate;
        rv->parseState().wb = &es.wb;
        rv->parseState().module = &mod;
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

::std::unique_ptr<TokenStream> ExpandMacro(const ExpandState& es, ASTModule& mod, ASTMacroInvocation& mi) {
    return ExpandMacro(es, mod, mi.span(), mi.path(), mi.inputIdent(), mi.inputTt());
}

void ExpandPattern(const ExpandState& es, ASTModule& mod, ASTPattern& pat, bool isRefutable) {
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
        }
        TU_ARMA(ValueLeftInc, e) {
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
            for (auto& sp : e.tupPat.start) {
                ExpandPattern(es, mod, sp, isRefutable);
            }
            for (auto& sp : e.tupPat.end) {
                ExpandPattern(es, mod, sp, isRefutable);
            }
        }
        TU_ARMA(Struct, e) {
            for (auto& subpat : e.subPatterns) {
                if (!ExpandAttrsCfgOnly(es, subpat.attrs)) {
                    subpat.name = RcString();
                    continue;
                }

                ExpandPattern(es, mod, subpat.pat, isRefutable);
            }
            auto newEnd = std::remove_if(e.subPatterns.begin(), e.subPatterns.end(), [&](const auto& e) {
                return e.name == "";
            });
            e.subPatterns.erase(newEnd, e.subPatterns.end());
        }
        TU_ARMA(Slice, e) {
            for (auto& sp : e.subPats) {
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

void ExpandType(const ExpandState& es, ASTModule& mod, ::ASTType*& ty) {
    TU_MATCH_HDRA( (ty->mData), {)
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
            ExpandType(es, mod, tf.mRettype);
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
            ExpandType(es, mod, e.inner);
        }
        TU_ARMA(Pointer, e) {
            ExpandType(es, mod, e.inner);
        }
        TU_ARMA(Array, e) {
            ExpandType(es, mod, e.inner);
            if (e.size) {
                ExpandExpr(es, e.size);
            }
        }
        TU_ARMA(Slice, e) {
            ExpandType(es, mod, e.inner);
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

void ExpandPathParams(const ExpandState& es, ASTModule& mod, ASTPathParams& params) {
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

void ExpandPath(const ExpandState& es, ASTModule& mod, ASTPath& p) {
    auto expandNodes = [&](::std::vector<ASTPathNode>& nodes) {
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
            ExpandType(es, mod, pe.type);
            if (pe.trait) {
                ExpandPath(es, mod, *pe.trait);
            }
            expandNodes(pe.nodes);
        }
    }
}

namespace {
    static ASTPath getPath(const RcString& coreCrate, const char* c1, const char* c2) {
        return ASTAbsolutePath(coreCrate, {RcString::newInterned(c1), RcString::newInterned(c2)});
    }

    static ASTPath getPath(const RcString& coreCrate, const char* c1, const char* c2, const char* c3) {
        return ASTAbsolutePath(coreCrate, {RcString::newInterned(c1), RcString::newInterned(c2), RcString::newInterned(c3)});
    }
}

struct CExpandExpr: public ASTNodeVisitor {
    ASTCrate& crate;
    const ExpandState& parentExpandState;
    ExpandState expandState;
    ASTExprNodeP replacement;

    // Stack of `try { ... }` blocks (the string is the loop label for the desugaring)
    ::std::vector<RcString> mTryStack;
    unsigned tryIndex = 0;

    ASTExprNodeBlock* currentBlock = nullptr;
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

    ASTModule& curMod() {
        return *const_cast<ASTModule*>(expandState.modstack.item);
    }

    void visit(ASTExprNodeP& cnode) {
        if (cnode.get()) {
            auto attrs = mv$(cnode->attrs());
            ExpandAttrsCfgAttr(*expandState.wb.settings, attrs);
            ExpandAttrs(expandState, attrs, AttrStage::Pre, [&](const Span& sp, const ExpandDecorator& d, const auto& a) {
                d.handle(sp, a, this->expandState.wb, this->crate, cnode);
            });
            if (cnode.get()) {
                cnode->attrs() = mv$(attrs);
            }
        }
        if (cnode.get()) {
            cnode->visit(*this);
            // If the node was a macro, and it was consumed, reset it
            if (auto* nMac = cast<ASTExprNodeMacro>(cnode.get())) {
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
                d.handle(sp, a, this->expandState.wb, this->crate, cnode);
            });
            if (cnode.get()) {
                cnode->attrs() = mv$(attrs);
            }
        }
        assert(!this->replacement);
    }

    void visitNodelete(const ASTExprNode& parent, ASTExprNodeP& cnode) {
        if (cnode.get() != nullptr) {
            this->visit(cnode);
            if (cnode.get() == nullptr) {
                ERROR(parent.span(), E0000, "#[cfg] not allowed in this position");
            }
        }
        assert(!this->replacement);
    }

    void visitVector(::std::vector<ASTExprNodeP>& cnodes) {
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

    ASTExprNodeP visitMacro(ASTExprNodeMacro& node, ::std::vector<ASTExprNodeBlock::Line>* nodesOut) {
        TRACE_FUNCTION_F(node.mPath << "!");
        if (!node.mPath.isValid()) {
            return ASTExprNodeP();
        }

        const bool definesMacro = node.mPath.isTrivial() && node.mPath.asTrivial() == "macro_rules";

        ASTExprNodeP rv;
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
                    auto marker = ASTExprNodeP(new ASTExprNodeMacroDefinition(it->data->definitionId, it->data->mHygiene, it->data->definitionHygiene));
                    marker->setSpan(node.span());
                    nodesOut->push_back({true, ::std::move(marker)});
                }
            }
            if (!nodesOut) {
                if (ttl->lookahead(0) != TOK_EOF) {
                    SET_MODULE((*ttl), mod);
                    DEBUG("-- Parsing as expression");
                    rv = ParseExpr0(*ttl);
                    if (ttl->lookahead(0) != TOK_EOF) {
                        ERROR(node.span(), E0000, "Unused tokens at the end of macro expansion - " << ttl->getToken());
                    }
                }
            } else {
                while (ttl->lookahead(0) != TOK_EOF) {
                    SET_MODULE((*ttl), mod);

                    // Reparse as statement / item
                    bool addSilenceIfEnd = false;
                    ::std::shared_ptr<ASTModule> tmpLocalMod;
                    auto& localModPtr = (this->currentBlock ? this->currentBlock->localMod : tmpLocalMod);
                    DEBUG("-- Parsing as statement line");
                    auto newexpr = ParseExprBlockLineWithItems(*ttl, localModPtr, addSilenceIfEnd);

                    if (tmpLocalMod) {
                        TODO(node.span(), "Handle edge case where a macro expansion outside of a _Block creates an item");
                    }

                    if (newexpr) {
                        nodesOut->push_back({addSilenceIfEnd, mv$(newexpr)});
                    } else {
                        // Expansion line just added a new item
                    }
                }
            }
            node.mPath = ASTPath();

            if (!nodesOut && !rv) {
                ERROR(node.span(), E0000, "Macro didn't expand to anything");
            }
        }

        return mv$(rv);
    }

    void visit(ASTExprNodeMacro& node) override {
        TRACE_FUNCTION_F("ExprNode_Macro - name = " << node.mPath);
        if (!node.mPath.isValid()) {
            return;
        }

        replacement = this->visitMacro(node, nullptr);

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

    void visit(ASTExprNodeBlock& node) override {
        this->inAssignLhs = false;
        unsigned int modItemCount = 0;

        auto prevModstack = this->expandState.modstack;
        if (node.localMod) {
            this->expandState.modstack = LList<const ASTModule*>(&prevModstack, node.localMod.get());
        }

        // TODO: macro_rules! invocations within the expression list influence this.
        // > Solution: Defer creation of the local module until during expand.
        if (node.localMod) {
            ExpandMod(this->expandState, node.localMod->path(), *node.localMod);
            modItemCount = node.localMod->mItems.size();
        }

        auto saved = this->currentBlock;
        this->currentBlock = &node;

        for (auto it = node.nodes.begin(); it != node.nodes.end();) {
            assert(it->node.get());

            if (auto* nodeMac = cast<ASTExprNodeMacro>(it->node.get())) {
                auto attrs = std::move(it->node->attrs());
                ExpandAttrsCfgAttr(*expandState.wb.settings, attrs);
                ExpandAttrs(expandState, attrs, AttrStage::Pre, [&](const Span& sp, const auto& d, const auto& a) {
                    d.handle(sp, a, this->expandState.wb, this->crate, it->node);
                });
                if (!it->node.get()) {
                    it = node.nodes.erase(it);
                    continue;
                }
                it->node->attrs() = std::move(attrs);

                assert(it->node.get() == nodeMac);

                ::std::vector<ASTExprNodeBlock::Line> newNodes;
                this->visitMacro(*nodeMac, &newNodes);
                for (const auto& n : newNodes) {
                    DEBUG("++ " << *n.node << (n.hasSemicolon ? " ;" : ""));
                }

                if (nodeMac->mPath.isValid()) {
                    DEBUG("Deferred macro");
                    ++it;
                } else {
                    // If this has a semicolon, then force the new final node to have a semicolon
                    if (it->hasSemicolon && !newNodes.empty()) {
                        newNodes.back().hasSemicolon = true;
                    }
                    it = node.nodes.erase(it);
                    it = node.nodes.insert(it, ::std::make_move_iterator(newNodes.begin()), ::std::make_move_iterator(newNodes.end()));
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

        this->currentBlock = saved;

        // HACK! Run Expand_Mod twice on local modules.
        if (node.localMod) {
            ExpandMod(this->expandState, node.localMod->path(), *node.localMod, modItemCount);
        }

        this->expandState.modstack = mv$(prevModstack);
    }

    void visit(ASTExprNodeAsyncBlock& node) override {
        this->visitNodelete(node, node.inner);
    }

    void visit(ASTExprNodeGeneratorBlock& node) override {
        this->visitNodelete(node, node.inner);
    }

    void visit(ASTExprNodeTry& node) override {
        // Desugar into
        // ```
        // loop '#tryNNN {
        //   break '#tryNNN { ... }
        // }
        // ```
        // NOTE: MIR lowering and HIR typecheck need to know to skip these (OR resolve should handle naming all loop blocks)
        mTryStack.push_back(RcString::newInterned(FMT("#try" << tryIndex++)));
        this->visitNodelete(node, node.inner);
        auto loopName = mv$(mTryStack.back());
        mTryStack.pop_back();

        auto coreCrate = crate.extCratenameCore;
        auto pathTry = getPath(coreCrate, "ops", "Try");
        auto pathTryFromOutput = ASTPath::newUfcsTrait(::mkType(*parentExpandState.crate.pool, node.span()), pathTry, {ASTPathNode(RcString::newInterned("from_output"))});
        auto okNode = ASTExprNodeP(new ASTExprNodeCallPath(mv$(pathTryFromOutput), ::makeVec1(mv$(node.inner))));
        auto breakNode = ASTExprNodeP(new ASTExprNodeFlow(ASTExprNodeFlow::BREAK, loopName, mv$(okNode)));
        this->replacement = ASTExprNodeP(new ASTExprNodeLoop(loopName, mv$(breakNode)));
    }

    void visit(ASTExprNodeAsm& node) override {
        for (auto& v : node.output) {
            this->visitNodelete(node, v.value);
        }
        for (auto& v : node.input) {
            this->visitNodelete(node, v.value);
        }
    }

    void visit(ASTExprNodeAsm2& node) override {
        for (auto& v : node.mParams) {
            TU_MATCH_HDRA((v), {)
            TU_ARMA(Const, e) {
                    this->visitNodelete(node, e);
                }
                TU_ARMA(Sym, e) {
                    ExpandPath(this->expandState, this->curMod(), e);
                }
                TU_ARMA(RegSingle, e) {
                    this->visitNodelete(node, e.val);
                }
                TU_ARMA(Reg, e) {
                    this->visitNodelete(node, e.valIn);
                    this->visitNodelete(node, e.valOut);
                }
            }
        }
    }

    void visit(ASTExprNodeFlow& node) override {
        this->visitNodelete(node, node.mValue);

        if (node.mType == ASTExprNodeFlow::YEET) {
            auto coreCrate = crate.extCratenameCore;
            auto pathOpsYeet = getPath(coreCrate, "ops", "Yeet");
            auto pathFromResidualFromResidual = getPath(coreCrate, "ops", "FromResidual", "from_residual");

            auto v = ASTExprNodeP(new ASTExprNodeCallPath(ASTPath(pathOpsYeet), ::makeVec1(std::move(node.mValue))));
            v->setSpan(node.span());
            v = ASTExprNodeP(new ASTExprNodeCallPath(ASTPath(pathFromResidualFromResidual), ::makeVec1(std::move(v))));
            v->setSpan(node.span());
            replacement = ASTExprNodeP(new ASTExprNodeFlow(
                (mTryStack.empty() ? ASTExprNodeFlow::RETURN : ASTExprNodeFlow::BREAK), // NOTE: uses `break 'tryblock` instead of return if in a try block.
                (mTryStack.empty() ? RcString("") : mTryStack.back()),
                std::move(v)
            ));
            replacement->setSpan(node.span());
        }
    }

    void visit(ASTExprNodeLetBinding& node) override {
        ExpandType(this->expandState, this->curMod(), node.mType);
        ExpandPattern(this->expandState, this->curMod(), node.pat, false);
        this->visitNodelete(node, node.mValue);
        this->visitNodelete(node, node.elseNode);
    }

    void visit(ASTExprNodeAssign& node) override {
        inAssignLhs = true;
        this->visitNodelete(node, node.slot);
        inAssignLhs = false;
        this->visitNodelete(node, node.mValue);

        // Desugar destructuring assignment
        // https://rust-lang.github.io/rfcs/2909-destructuring-assignment.html
        if (node.op == ASTExprNodeAssign::NONE) {
            struct VisitorToPat: public ASTNodeVisitor {
                std::vector<std::pair<RcString, ASTExprNodeP>> slots;
                ASTPattern mRv;

                bool mRvSet = false;
                bool mIsSlot = false;

                ASTPattern lower(ASTExprNodeP& ep) {
                    assert(ep);
                    ep->visit(*this);
                    ASSERT_BUG(ep->span(), mRvSet, ep.typeName() << " - Didn't yield a pattern");
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
                void pat(ASTPattern rv) {
                    assert(!mRvSet);
                    assert(!mIsSlot);
                    mRvSet = true;
                    assert(rv.bindings().empty());
                    mRv = std::move(rv);
                }

                // - This is a slot (to be assigned)
                void slot(ASTExprNode& v) {
                    mRvSet = true;
                    mIsSlot = true;

                    RcString name(FMT("_#" << slots.size()).c_str());
                    slots.push_back(std::make_pair(name, ASTExprNodeP()));
                    mRv = ASTPattern(ASTPattern::TagBind(), v.span(), slots.back().first);
                }

                // - The given node isn't valid on the LHS of an assignment
                void invalid(const ASTExprNode& v) {
                    ERROR(v.span(), E0000, typeid(v).name() << " isn't valid on the LHS of an assignemnt");
                }

                void visit(ASTExprNodeBlock& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeAsyncBlock& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeGeneratorBlock& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeTry& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeMacro& v) override {
                    BUG(v.span(), "Encountered macro");
                }

                void visit(ASTExprNodeAsm& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeAsm2& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeFlow& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeLetBinding& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeAssign& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeCallPath& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeCallMethod& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeCallObject& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeLoop& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeFor& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeWhile& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeMatch& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeIf& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeWildcardPattern& v) override {
                    mRvSet = true;
                    mRv = ASTPattern(v.span(), ASTPattern::Data::make_Any({}));
                }

                void visit(ASTExprNodeInteger& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeFloat& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeBool& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeString& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeByteString& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeCString& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeClosure& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeStructLiteral& v) override {
                    if (v.baseValue) {
                        TODO(v.span(), "Struct literal with `..value` in destructured assignment");
                    }
                    std::vector<ASTStructPatternEntry> subpats;
                    for (auto& m : v.values) {
                        subpats.push_back(ASTStructPatternEntry{std::move(m.attrs), m.name, lower(m.value)});
                    }
                    pat(ASTPattern(ASTPattern::TagStruct(), v.span(), v.mPath, std::move(subpats), true));
                }

                void visit(ASTExprNodeStructLiteralPattern& v) override {
                    std::vector<ASTStructPatternEntry> subpats;
                    for (auto& m : v.values) {
                        subpats.push_back(ASTStructPatternEntry{std::move(m.attrs), m.name, lower(m.value)});
                    }
                    pat(ASTPattern(ASTPattern::TagStruct(), v.span(), v.mPath, std::move(subpats), false));
                }

                void visit(ASTExprNodeArray& v) override {
                    if (v.mSize) {
                        TODO(v.span(), "Sized Array literal in destructured assignment");
                    } else {
                        std::vector<ASTPattern> subpats;
                        for (auto& m : v.values) {
                            subpats.push_back(lower(m));
                        }
                        pat(ASTPattern(v.span(), ASTPattern::Data::make_Slice({std::move(subpats)})));
                    }
                }

                void visit(ASTExprNodeTuple& v) override {
                    bool isSplit = false;
                    std::vector<ASTPattern> subpatsStart;
                    std::vector<ASTPattern> subpats;
                    for (auto& m : v.values) {
                        if (const auto* e = cast<ASTExprNodeBinOp>(m.get())) {
                            if (e->mType == ASTExprNodeBinOp::RANGE && !e->left && !e->right) {
                                ASSERT_BUG(v.span(), !isSplit, "Multiple `..` in tuple pattern?");
                                isSplit = true;
                                subpatsStart = std::move(subpats);
                                continue;
                            }
                        }
                        subpats.push_back(lower(m));
                    }
                    if (isSplit) {
                        pat(ASTPattern(ASTPattern::TagTuple(), v.span(), ASTPattern::TuplePat{std::move(subpatsStart), true, std::move(subpats)}));
                    } else {
                        pat(ASTPattern(ASTPattern::TagTuple(), v.span(), std::move(subpats)));
                    }
                }

                // Just emit as if it's a slot, `UnitStruct = Foo` isn't valid
                void visit(ASTExprNodeNamedValue& v) override {
                    slot(v);
                }

                void visit(ASTExprNodeField& v) override {
                    slot(v);
                }

                void visit(ASTExprNodeIndex& v) override {
                    slot(v);
                }

                void visit(ASTExprNodeDeref& v) override {
                    slot(v);
                }

                void visit(ASTExprNodeCast& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeTypeAnnotation& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeBinOp& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeUniOp& v) override {
                    invalid(v);
                }

                void visit(ASTExprNodeMacroDefinition& v) override {
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
                auto rv = new ASTExprNodeBlock();
                rv->nodes.push_back({true, ASTExprNodeP(new ASTExprNodeLetBinding(std::move(pat), mkType(*parentExpandState.crate.pool, node.span()), std::move(node.mValue)))});
                for (auto& slots : v.slots) {
                    rv->nodes.push_back({true, ASTExprNodeP(new ASTExprNodeAssign(ASTExprNodeAssign::NONE, std::move(slots.second), ASTExprNodeP(new ASTExprNodeNamedValue(ASTPath::newLocal(std::move(slots.first))))))});
                }
                this->replacement = ASTExprNodeP(rv);
            }
        }
    }

    void visit(ASTExprNodeCallPath& node) override {
        ExpandPath(this->expandState, this->curMod(), node.mPath);
        this->visitVector(node.mArgs);
    }

    void visit(ASTExprNodeCallMethod& node) override {
        ExpandPathParams(this->expandState, this->curMod(), node.method.args());
        this->visitNodelete(node, node.val);
        this->visitVector(node.mArgs);
    }

    void visit(ASTExprNodeCallObject& node) override {
        this->visitNodelete(node, node.val);
        this->visitVector(node.mArgs);
    }

    void visit(ASTExprNodeLoop& node) override {
        this->visitNodelete(node, node.mCode);
    }

    void visit(ASTExprNodeFor& node) override {
        ExpandPattern(this->expandState, this->curMod(), node.pattern, false);
        this->visitNodelete(node, node.mValue);
        this->visitNodelete(node, node.mCode);

        static const RcString rcstringIntoIter = RcString::newInterned("into_iter");
        static const RcString rcstringNext = RcString::newInterned("next");
        static const RcString rcstringIt = RcString::newInterned("it");
        const auto iteratorHygiene = Ident::Hygiene::newScope(*parentExpandState.crate.pool);
        auto coreCrate = crate.extCratenameCore;
        auto pathSome = getPath(coreCrate, "option", "Option", "Some");
        auto pathNone = getPath(coreCrate, "option", "Option", "None");
        auto pathIntoIterator = getPath(coreCrate, "iter", "IntoIterator");
        auto pathIterator = getPath(coreCrate, "iter", "Iterator");
        // Desugar into:
        // {
        //         `m_label`: loop {
        //             Some(`m_pattern`) => `m_code`,
        //             None => break `m_label`,
        //             }
        //         }
        //     }
        // }
        ::std::vector<ASTExprNodeMatchArm> arms;
        // - `Some(pattern ) => code`
        arms.push_back(ASTExprNodeMatchArm(::makeVec1(ASTPattern(ASTPattern::TagNamedTuple(), node.span(), pathSome, ::makeVec1(mv$(node.pattern)))), {}, mv$(node.mCode)));
        // - `None => break label`
        arms.push_back(ASTExprNodeMatchArm(::makeVec1(ASTPattern(ASTPattern::TagValue(), node.span(), ASTPattern::Value::make_Named(pathNone))), {}, ASTExprNodeP(new ASTExprNodeFlow(ASTExprNodeFlow::BREAK, node.label, nullptr))));

        auto nextReceiver = ASTExprNodeP(new ASTExprNodeNamedValue(ASTPath::newRelative(iteratorHygiene, ::makeVec1(ASTPathNode(rcstringIt)))));
        auto nextReceiverBorrow = ASTExprNodeP(new ASTExprNodeUniOp(ASTExprNodeUniOp::REFMUT, mv$(nextReceiver)));
        auto nextCall = ASTExprNodeP(new ASTExprNodeCallPath(ASTPath::newUfcsTrait(::mkType(*parentExpandState.crate.pool, node.span()), pathIterator, {ASTPathNode(rcstringNext)}), ::makeVec1(mv$(nextReceiverBorrow))));
        auto nextMatch = ASTExprNodeP(new ASTExprNodeMatch(mv$(nextCall), mv$(arms)));
        auto loop = ASTExprNodeP(new ASTExprNodeLoop(node.label, mv$(nextMatch)));

        auto intoIterCall = ASTExprNodeP(new ASTExprNodeCallPath(ASTPath::newUfcsTrait(::mkType(*parentExpandState.crate.pool, node.span()), pathIntoIterator, {ASTPathNode(rcstringIntoIter)}), ::makeVec1(mv$(node.mValue))));
        auto outerMatch = ASTExprNodeP(new ASTExprNodeMatch(mv$(intoIterCall), ::makeVec1(ASTExprNodeMatchArm(::makeVec1(ASTPattern(ASTPattern::TagBind(), node.span(), Ident(iteratorHygiene, rcstringIt))), {}, mv$(loop)))));

        // rustc wraps the outer match in `DropTemps`: for always yields (), so
        // a block containing the match as a statement provides the same
        // terminating temporary scope without leaking head temporaries into a
        // surrounding initializer or call expression.
        auto block = new ASTExprNodeBlock();
        block->nodes.push_back({true, mv$(outerMatch)});
        replacement.reset(block);
        replacement->setSpan(node.span());
    }

    void visit(ASTExprNodeWhile& node) override {
        for (auto& cond : node.conditions) {
            if (cond.optPat) {
                ExpandPattern(this->expandState, this->curMod(), *cond.optPat, true);
            }
            this->visitNodelete(node, cond.value);
        }
        this->visitNodelete(node, node.mCode);
    }

    void visit(ASTExprNodeMatch& node) override {
        this->visitNodelete(node, node.val);
        for (auto& arm : node.arms) {
            ExpandAttrsCfgAttr(*expandState.wb.settings, arm.mAttrs);
            ExpandAttrs(expandState, arm.mAttrs, AttrStage::Pre, [&](const Span& sp, const auto& d, const auto& a) {
                d.handle(sp, a, expandState.wb, crate, arm);
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
                this->visitNodelete(node, cond.value);
            }

            this->visitNodelete(node, arm.mCode);
            ExpandAttrs(expandState, arm.mAttrs, AttrStage::Post, [&](const Span& sp, const auto& d, const auto& a) {
                d.handle(sp, a, expandState.wb, crate, arm);
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

    void visit(ASTExprNodeIf& node) override {
        for (auto& arm : node.arms) {
            for (auto& cond : arm.conditions) {
                if (cond.optPat) {
                    ExpandPattern(this->expandState, this->curMod(), *cond.optPat, true);
                }
                this->visitNodelete(node, cond.value);
            }
            this->visitNodelete(node, arm.body);
        }
        this->visitNodelete(node, node.elseNode);
    }

    void visit(ASTExprNodeWildcardPattern& node) override {
    }

    void visit(ASTExprNodeInteger& node) override {
    }

    void visit(ASTExprNodeFloat& node) override {
    }

    void visit(ASTExprNodeBool& node) override {
    }

    void visit(ASTExprNodeString& node) override {
    }

    void visit(ASTExprNodeByteString& node) override {
    }

    void visit(ASTExprNodeCString& node) override {
    }

    void visit(ASTExprNodeClosure& node) override {
        auto tryStack = ::std::move(mTryStack);
        for (auto& arg : node.mArgs) {
            ExpandPattern(this->expandState, this->curMod(), arg.first, false);
            ExpandType(this->expandState, this->curMod(), arg.second);
        }
        ExpandType(this->expandState, this->curMod(), node.returnType);
        this->visitNodelete(node, node.mCode);
        mTryStack = std::move(tryStack);
    }

    void visit(ASTExprNodeStructLiteral& node) override {
        this->visitNodelete(node, node.baseValue);
        for (auto& val : node.values) {
            ExpandAttrsCfgAttr(*expandState.wb.settings, val.attrs);
            ExpandAttrs(expandState, val.attrs, AttrStage::Pre, [&](const Span& sp, const auto& d, const auto& a) {
                d.handle(sp, a, expandState.wb, crate, val);
            });
            if (!val.value) {
                continue;
            }
            this->visitNodelete(node, val.value);
            ExpandAttrs(expandState, val.attrs, AttrStage::Post, [&](const Span& sp, const auto& d, const auto& a) {
                d.handle(sp, a, expandState.wb, crate, val);
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

    void visit(ASTExprNodeStructLiteralPattern& node) override {
        for (auto& val : node.values) {
            ExpandAttrsCfgAttr(*expandState.wb.settings, val.attrs);
            ExpandAttrs(expandState, val.attrs, AttrStage::Pre, [&](const Span& sp, const auto& d, const auto& a) {
                d.handle(sp, a, expandState.wb, crate, val);
            });
            if (!val.value) {
                continue;
            }
            this->visitNodelete(node, val.value);
            ExpandAttrs(expandState, val.attrs, AttrStage::Post, [&](const Span& sp, const auto& d, const auto& a) {
                d.handle(sp, a, expandState.wb, crate, val);
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

    void visit(ASTExprNodeArray& node) override {
        this->visitNodelete(node, node.mSize);
        this->visitVector(node.values);
    }

    void visit(ASTExprNodeTuple& node) override {
        this->visitVector(node.values);
    }

    void visit(ASTExprNodeNamedValue& node) override {
        ExpandPath(this->expandState, this->curMod(), node.mPath);
    }

    void visit(ASTExprNodeField& node) override {
        this->visitNodelete(node, node.obj);
    }

    void visit(ASTExprNodeIndex& node) override {
        this->inAssignLhs = false;
        this->visitNodelete(node, node.obj);
        this->visitNodelete(node, node.idx);
    }

    void visit(ASTExprNodeDeref& node) override {
        this->visitNodelete(node, node.mValue);
    }

    void visit(ASTExprNodeCast& node) override {
        this->visitNodelete(node, node.mValue);
        ExpandType(this->expandState, this->curMod(), node.mType);
    }

    void visit(ASTExprNodeTypeAnnotation& node) override {
        this->visitNodelete(node, node.mValue);
        ExpandType(this->expandState, this->curMod(), node.mType);
    }

    void visit(ASTExprNodeBinOp& node) override {
        this->visitNodelete(node, node.left);
        this->visitNodelete(node, node.right);

        if (this->inAssignLhs) {
            return;
        }
        static const RcString rcstringStart = RcString::newInterned("start");
        static const RcString rcstringEnd = RcString::newInterned("end");
        switch (node.mType) {
            case ASTExprNodeBinOp::RANGE: {
                // NOTE: Not language items pre 1.39
                auto coreCrate = crate.extCratenameCore;
                auto pathRange = getPath(coreCrate, "ops", "Range");
                auto pathRangeFrom = getPath(coreCrate, "ops", "RangeFrom");
                auto pathRangeTo = getPath(coreCrate, "ops", "RangeTo");
                auto pathRangeFull = getPath(coreCrate, "ops", "RangeFull");

                ASTExprNodeStructLiteral::tValues values;
                if (node.left && node.right) {
                    values.push_back({{}, rcstringStart, mv$(node.left)});
                    values.push_back({{}, rcstringEnd, mv$(node.right)});
                    replacement.reset(new ASTExprNodeStructLiteral(mv$(pathRange), nullptr, mv$(values)));
                } else if (node.left) {
                    values.push_back({{}, rcstringStart, mv$(node.left)});
                    replacement.reset(new ASTExprNodeStructLiteral(mv$(pathRangeFrom), nullptr, mv$(values)));
                } else if (node.right) {
                    values.push_back({{}, rcstringEnd, mv$(node.right)});
                    replacement.reset(new ASTExprNodeStructLiteral(mv$(pathRangeTo), nullptr, mv$(values)));
                } else {
                    replacement.reset(new ASTExprNodeStructLiteral(mv$(pathRangeFull), nullptr, mv$(values)));
                }
                replacement->setSpan(node.span());
                break;
            }
            case ASTExprNodeBinOp::RANGE_INC: {
                // NOTE: Not language items pre 1.54
                auto coreCrate = crate.extCratenameCore;
                auto pathRangeInclusiveNonEmpty = getPath(coreCrate, "ops", "RangeInclusive");
                auto pathRangeToInclusive = getPath(coreCrate, "ops", "RangeToInclusive");

                if (node.left) {
                    ASTExprNodeStructLiteral::tValues values;
                    values.push_back({{}, rcstringStart, mv$(node.left)});
                    values.push_back({{}, rcstringEnd, mv$(node.right)});
                    values.push_back({{}, RcString::newInterned("exhausted"), ASTExprNodeP(new ASTExprNodeBool(false))});
                    replacement.reset(new ASTExprNodeStructLiteral(mv$(pathRangeInclusiveNonEmpty), nullptr, mv$(values)));
                } else {
                    ASTExprNodeStructLiteral::tValues values;
                    values.push_back({{}, rcstringEnd, mv$(node.right)});
                    replacement.reset(new ASTExprNodeStructLiteral(mv$(pathRangeToInclusive), nullptr, mv$(values)));
                }
                replacement->setSpan(node.span());
                break;
            }
            default:
                break;
        }
    }

    void visit(ASTExprNodeUniOp& node) override {
        this->visitNodelete(node, node.mValue);
        // - Desugar question mark operator before resolve so it can create names
        if (node.mType == ASTExprNodeUniOp::QMARK) {
            auto coreCrate = crate.extCratenameCore;

            // TODO: Find a way of creating bindings during HIR lower instead (so lang items are available)

            auto pathTry = getPath(coreCrate, "ops", "Try");
            static const RcString rcstringV = RcString::newInterned("v");
            static const RcString rcstringR = RcString::newInterned("r");
            // TryV2
            {
                auto pathTryBranch = ASTPath::newUfcsTrait(::mkType(*parentExpandState.crate.pool, node.span()), pathTry, {ASTPathNode(RcString::newInterned("branch"))});
                // Not a lang item
                auto path_ControlFlow_Continue = getPath(coreCrate, "ops", "ControlFlow", "Continue");
                auto path_ControlFlow_Break = getPath(coreCrate, "ops", "ControlFlow", "Break");
                auto pathFromResidualFromResidual = getPath(coreCrate, "ops", "FromResidual", "from_residual");

                ::std::vector<ASTExprNodeMatchArm> arms;
                // `Continue(v) => v,`
                arms.push_back(ASTExprNodeMatchArm(::makeVec1(ASTPattern(ASTPattern::TagNamedTuple(), node.span(), path_ControlFlow_Continue, ::makeVec1(ASTPattern(ASTPattern::TagBind(), node.span(), rcstringV)))), {}, ASTExprNodeP(new ASTExprNodeNamedValue(ASTPath(rcstringV)))));
                // `Break(r) => return R::from_residual(r),`
                arms.push_back(ASTExprNodeMatchArm(
                    ::makeVec1(ASTPattern(ASTPattern::TagNamedTuple(), node.span(), path_ControlFlow_Break, ::makeVec1(ASTPattern(ASTPattern::TagBind(), node.span(), rcstringR)))),
                    {},
                    ASTExprNodeP(new ASTExprNodeFlow(
                        (mTryStack.empty() ? ASTExprNodeFlow::RETURN : ASTExprNodeFlow::BREAK), // NOTE: uses `break 'tryblock` instead of return if in a try block.
                        (mTryStack.empty() ? RcString("") : mTryStack.back()),
                        ASTExprNodeP(new ASTExprNodeCallPath(ASTPath(pathFromResidualFromResidual), ::makeVec1(ASTExprNodeP(new ASTExprNodeNamedValue(ASTPath(rcstringR))))))
                    ))
                ));

                replacement.reset(new ASTExprNodeMatch(ASTExprNodeP(new ASTExprNodeCallPath(mv$(pathTryBranch), ::makeVec1(mv$(node.mValue)))), mv$(arms)));
            }
        }
    }

    void visit(ASTExprNodeMacroDefinition&) override {
    }
};

void ExpandExpr(const ExpandState& es, ASTExprNodeP& node) {
    TRACE_FUNCTION_F("unique_ptr");
    CExpandExpr visitor{es};
    visitor.visit(node);
}

void ExpandExpr(const ExpandState& es, ::std::shared_ptr<ASTExprNode>& node) {
    TRACE_FUNCTION_F("shared_ptr");
    CExpandExpr visitor{es};
    node->visit(visitor);
    if (visitor.replacement) {
        node.reset(visitor.replacement.release());
    }
}

void ExpandExpr(const ExpandState& es, ASTExpr& node) {
    TRACE_FUNCTION_F("AST::Expr");
    CExpandExpr visitor{es};
    node.visitNodes(visitor);
    if (visitor.replacement) {
        node = ASTExpr(mv$(visitor.replacement));
    }
}

void ExpandGenericParams(const ExpandState& es, ASTModule& mod, ASTGenericParams& params) {
    for (auto& paramDef : params.mParams) {
        TU_MATCH_HDRA( (paramDef), {)
        TU_ARMA(None, e) {
                // Ignore
            }
            TU_ARMA(Lifetime, e) {
            }
            TU_ARMA(Type, tyDef) {
                ExpandType(es, mod, tyDef.getDefault());
            }
            TU_ARMA(Value, valDef) {
                ExpandType(es, mod, valDef.type());
            }
        }
    }
    for (auto& bound : params.bounds) {
        TU_MATCHA((bound), (be), (None, ), (Lifetime, ), (TypeLifetime, ExpandType(es, mod, be.type);), (IsTrait, ExpandType(es, mod, be.type); ExpandPath(es, mod, be.trait);), (MaybeTrait, ExpandType(es, mod, be.type); ExpandPath(es, mod, be.trait);), (NotTrait, ExpandType(es, mod, be.type); ExpandPath(es, mod, be.trait);), (Equality, ExpandType(es, mod, be.type); ExpandType(es, mod, be.replacement);))
    }
    for (auto& t : params.mBareBoundTypes) {
        ExpandType(es, mod, t);
    }
}

void ExpandBareExpr(const WireBoard& wb, const ASTCrate& crate, const ASTModule& mod, ASTExprNodeP& node) {
    ExpandState es{wb, const_cast<ASTCrate&>(crate), LList<const ASTModule*>(nullptr, &mod), ExpandMode::FirstPass};
    ExpandExpr(es, node);
    es.mode = ExpandMode::Final;
}

ASTExprNodeP ExpandParseAndExpandExprVal(const ASTCrate& crate, const ASTModule& mod, TokenStream& lex) {
    auto sp = lex.pointSpan();
    auto n = ParseExprVal(lex);
    ASSERT_BUG(sp, n, "No expression returned");
    ExpandBareExpr(*lex.parseState().wb, crate, mod, n);
    return n;
}

void ExpandFunction(const ExpandState& es, ASTModule& mod, ASTFunction& e) {
    for (size_t i = 0; i < e.args().size(); i++) {
        auto& arg = e.args()[i];
        if (!ExpandAttrsCfgOnly(es, arg.attrs)) {
            e.args().erase(e.args().begin() + i);
            i--;
            continue;
        }
        ExpandPattern(es, mod, arg.pat, false);
        ExpandType(es, mod, arg.ty);
        ExpandAttrs(es, arg.attrs, AttrStage::Post, [&](const Span& sp, const ExpandDecorator& d, const ASTAttribute& a) {
            TODO(sp, "attributes on function arguments - " << a);
        });
    }
    ExpandType(es, mod, e.rettype());
    ExpandExpr(es, e.code());
}

void Expand_Impl(const ExpandState& es, ASTPath modpath, ASTModule& mod, ASTImpl& impl) {
    TRACE_FUNCTION_F(impl.def());
    ExpandGenericParams(es, mod, impl.def().params());

    ExpandType(es, mod, impl.def().type());
    ExpandPath(es, mod, impl.def().trait().ent);

    DEBUG("> Items");
    for (unsigned int idx = 0; idx < impl.items().size(); idx++) {
        auto i = std::move(impl.items()[idx]);
        DEBUG("  - " << i.name << " :: " << i.attrs);

        // TODO: Make a path from the impl definition? Requires having the impl def resolved to be correct
        // - Does it? the namespace is essentially the same. There may be issues with wherever the path is used though
        // TODO: UFCS path, or different method
        ASTAbsolutePath path("", {"", i.name});

        auto attrs = mv$(i.attrs);
        ExpandAttrsCfgAttr(*es.wb.settings, attrs);
        ExpandAttrs(es, attrs, AttrStage::Pre, mod, impl, i.vis, i.name, *i.data);

        TU_MATCH_HDRA( (*i.data), {)
        default:
            BUG(Span(), "Unknown item type in impl block - " << i.data->tagStr());
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

                        *i.data = ASTItem::make_None({});
                    } else {
                        // Move back in (using the index, as the old pointr may be invalid)
                        i.data->as_MacroInv() = mv$(miOwned);
                    }
                }
            }
            TU_ARMA(Function, e) {
                TRACE_FUNCTION_F("fn " << i.name);
                ExpandFunction(es, mod, e);
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

void Expand_ImplDef(const ExpandState& es, ASTPath modpath, ASTModule& mod, ASTImplDef& implDef) {
    ExpandGenericParams(es, mod, implDef.params());

    ExpandType(es, mod, implDef.type());
}

void Expand_ExternBlock(const ExpandState& es, ASTModule& mod, ASTExternBlock& block) {
    TRACE_FUNCTION_F("ABI=" << block.abi());
    for (size_t idx = 0; idx < block.items().size(); idx++) {
        auto& i = block.items()[idx];
        DEBUG(i.data.tagStr() << " " << mod.path() << "::" << i.name);

        auto path = mod.path() + i.name;

        auto attrs = mv$(i.attrs);
        auto vis = i.vis;

        auto dat = std::move(i.data);
        TU_MATCH_HDRA( (dat), { )
        default:
            BUG(Span(), "Unexpected item type - " << dat.tagStr());
            TU_ARMA(None, e) {
                // Skip: nothing
            }

            TU_ARMA(Type, e) {
                ExpandType(es, mod, e.type());
            }
            TU_ARMA(Function, e) {
                ExpandFunction(es, mod, e);
            }
            TU_ARMA(Static, e) {
                ExpandExpr(es, e.value());
                ExpandType(es, mod, e.type());
            }

            TU_ARMA(MacroInv, e) {
                // Move out of the module to avoid invalidation if a new macro invocation is added
                auto miOwned = mv$(e);

                if (!miOwned.isExpanded()) {
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

                        miOwned.setExpanded();
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

void ExpandMod(const ExpandState& es, ASTAbsolutePath modpath, ASTModule& mod, unsigned int firstItem) {
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
                    mod.macroImports.push_back(ASTModule::MacroImport{false, mac.name, es.modstack.prev->item->path() + mac.name, &*mac.data});
                    DEBUG(mod.path() << " + Import defined '" << mod.macroImports.back().path << "'");
                }
            }
        }

        // Insert prelude if: Enabled for this module, present for the crate, and this module is not an anon
        if (es.crate.preludePath != ASTPath()) {
            if (mod.insertPrelude && !mod.isAnon()) {
                DEBUG("> Adding custom prelude " << es.crate.preludePath);
                mod.addItem(Span(), ASTVisibility::makeRestricted(ASTVisibility::Ty::Private, mod.path()), "", ASTUseItem{Span(), ::makeVec1(ASTUseItem::Ent{Span(), es.crate.preludePath, ""})}, {});
            } else {
                DEBUG("> Not inserting custom prelude (anon or disabled)");
            }
        }
    }

    // Stack to prevent macro recursion
    // - Items are popped if the item address matches
    std::vector<const ASTNamed<ASTItem>*> macroRecursionStack;

    DEBUG("Items");
    for (unsigned int idx = firstItem; idx < mod.mItems.size(); idx++) {
        auto& i = *mod.mItems[idx];

        // If this is the pop point for this entry, then pop
        // - Note, can be `nullptr`, but that indicates that the macro invocation was the end
        while (!macroRecursionStack.empty() && macroRecursionStack.back() == &i) {
            macroRecursionStack.pop_back();
            DEBUG("End macro recursion guard");
        }

        DEBUG("- " << modpath << "::" << i.name << " (" << ASTItem::tagToStr(i.data.tag()) << ") :: " << i.attrs);
        auto path = modpath + i.name;

        if (const auto* mi = i.data.opt_MacroInv()) {
            if (mi->path().isTrivial() && mi->path().asTrivial() == "macro_rules") {
                i.vis = ASTVisibility::makeGlobal();
                DEBUG("macro_rules made pub");
            }
        }

        // Pre-exapand inner `#[cfg]`
        {
            struct H {
                static void filterCfg(const Settings& settings, ::std::vector<ASTStructItem>& lst) {
                    auto newEnd = ::std::remove_if(lst.begin(), lst.end(), [&](const ASTStructItem& v) {
                        return !checkCfgAttrs(settings, v.mAttrs);
                    });
                    DEBUG(lst.size() << " -> " << newEnd - lst.begin());
                    lst.erase(newEnd, lst.end());
                }

                static void filterCfg(const Settings& settings, ::std::vector<ASTTupleItem>& lst) {
                    auto newEnd = ::std::remove_if(lst.begin(), lst.end(), [&](const ASTTupleItem& v) {
                        return !checkCfgAttrs(settings, v.mAttrs);
                    });
                    DEBUG(lst.size() << " -> " << newEnd - lst.begin());
                    lst.erase(newEnd, lst.end());
                }
            };

            DEBUG(i.data.tagStr() << " " << mod.path() << "::" << i.name);
            TU_MATCH_HDRA( (i.data), { )
            // Expand cfg within types, so derive macros don't need to care
            TU_ARMA(Struct, str) {
                TU_MATCH_HDRA((str.mData), {)
                TU_ARMA(Unit, e) {
                        }
                        TU_ARMA(Struct, e) {
                            H::filterCfg(*es.wb.settings, e.ents);
                        }
                        TU_ARMA(Tuple, e) {
                            H::filterCfg(*es.wb.settings, e.ents);
                        }
                }
                }
                TU_ARMA(Union, unm) {
                    H::filterCfg(*es.wb.settings, unm.mVariants);
                }
                TU_ARMA(Enum, enm) {
                    for (auto it = enm.variants().begin(); it != enm.variants().end();) {
                        if (!checkCfgAttrs(*es.wb.settings, it->mAttrs)) {
                            it = enm.variants().erase(it);
                        } else {
                        TU_MATCH_HDRA( (it->mData), { )
                        TU_ARMA(Unit, e) {
                                }
                                TU_ARMA(Tuple, e) {
                                    H::filterCfg(*es.wb.settings, e.mItems);
                                }
                                TU_ARMA(Struct, e) {
                                    H::filterCfg(*es.wb.settings, e.fields);
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
        ExpandAttrsCfgAttr(*es.wb.settings, attrs);
        ExpandAttrs(es, attrs, AttrStage::Pre, path, mod, idx, vis, i.data);

        // Do modules without moving the definition (so the module path is always valid)
        if (i.data.is_Module()) {
            auto& e = i.data.as_Module();
            LList<const ASTModule*> subModstack(&es.modstack, &e);
            ExpandState esInner(es.wb, es.crate, subModstack, es.mode);
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

                if (!miOwned.isExpanded()) {
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

                        miOwned.setExpanded();
                    } else {
                        DEBUG("Deferred macro");
                    }
                }
                dat.as_MacroInv() = mv$(miOwned);
            }
            TU_ARMA(Macro, e) {
                ASSERT_BUG(i.span, e, "Null macro - " << i.name);
                mod.addMacro(i.vis.isGlobal(), i.name, mv$(e));
                dat = ASTItem::make_None({});
            }
            TU_ARMA(Use, e) {
                // Determine if the `use` refers to a macro, and import into the current scope
                for (const auto& ue : e.entries) {
                    // Get module ref, if it's to a HIR module then grab the macro
                    if (ue.name != "" && ue.path.nodes().size() >= 1) {
                        DEBUG("Use " << ue.path);

                        ASTAbsolutePath refPath;
                        auto m = ResolveLookupMacro(ue.sp, *es.wb.settings, es.crate, mod.path(), ue.path, /*out_path=*/&refPath);
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
                            mod.macroImports.push_back(ASTModule::MacroImport{false, ue.name, std::move(refPath), std::move(ref)});
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
                        e.name = es.crate.loadExternCrate(*es.wb.settings, i.span, e.name);
                    }
                    // Crates imported in root are added to the implicit list
                    if (modpath.nodes.empty()) {
                        es.wb.settings->implicitCrates.insert(std::make_pair(i.name, e.name));
                    }
                } else {
                    if (modpath.nodes.empty()) {
                        es.wb.settings->implicitCrates.insert(std::make_pair(i.name, ""));
                    }
                }
            }

            TU_ARMA(Struct, e) {
                ExpandGenericParams(es, mod, e.params());
            TU_MATCH_HDRA( (e.mData), {)
            TU_ARMA(Unit, sd) {
                    }
                    TU_ARMA(Struct, sd) {
                        for (auto it = sd.ents.begin(); it != sd.ents.end();) {
                            auto& si = *it;
                            ExpandAttrsCfgAttr(*es.wb.settings, si.mAttrs);
                            ExpandAttrs(es, si.mAttrs, AttrStage::Pre, [&](const Span& sp, const auto& d, const auto& a) {
                                d.handle(sp, a, es.wb, es.crate, si);
                            });
                            ExpandType(es, mod, si.mType);
                            ExpandExpr(es, si.defaultValue);
                            ExpandAttrs(es, si.mAttrs, AttrStage::Post, [&](const Span& sp, const auto& d, const auto& a) {
                                d.handle(sp, a, es.wb, es.crate, si);
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
                            ExpandAttrsCfgAttr(*es.wb.settings, si.mAttrs);
                            ExpandAttrs(es, si.mAttrs, AttrStage::Pre, [&](const Span& sp, const auto& d, const auto& a) {
                                d.handle(sp, a, es.wb, es.crate, si);
                            });
                            ExpandType(es, mod, si.mType);
                            ExpandAttrs(es, si.mAttrs, AttrStage::Post, [&](const Span& sp, const auto& d, const auto& a) {
                                d.handle(sp, a, es.wb, es.crate, si);
                            });

                            if (!si.mType->isValid()) {
                                it = sd.ents.erase(it);
                            } else {
                                ++it;
                            }
                        }
                    }
            }
            }
            TU_ARMA(Enum, e) {
                ExpandGenericParams(es, mod, e.params());
                for (auto& var : e.variants()) {
                    ExpandAttrsCfgAttr(*es.wb.settings, var.mAttrs);
                    ExpandAttrs(es, var.mAttrs, AttrStage::Pre, [&](const Span& sp, const auto& d, const auto& a) {
                        d.handle(sp, a, es.wb, es.crate, var);
                    });
                TU_MATCH_HDRA( (var.mData), {)
                TU_ARMA(Unit, e) {
                        }
                        TU_ARMA(Tuple, e) {
                            for (auto it = e.mItems.begin(); it != e.mItems.end();) {
                                auto& si = *it;
                                ExpandAttrsCfgAttr(*es.wb.settings, si.mAttrs);
                                ExpandAttrs(es, si.mAttrs, AttrStage::Pre, [&](const Span& sp, const auto& d, const auto& a) {
                                    d.handle(sp, a, es.wb, es.crate, si);
                                });
                                ExpandType(es, mod, si.mType);
                                ExpandAttrs(es, si.mAttrs, AttrStage::Pre, [&](const Span& sp, const auto& d, const auto& a) {
                                    d.handle(sp, a, es.wb, es.crate, si);
                                });
                                if (!si.mType->isValid()) {
                                    it = e.mItems.erase(it);
                                } else {
                                    ++it;
                                }
                            }
                        }
                        TU_ARMA(Struct, e) {
                            for (auto it = e.fields.begin(); it != e.fields.end();) {
                                auto& si = *it;
                                ExpandAttrsCfgAttr(*es.wb.settings, si.mAttrs);
                                ExpandAttrs(es, si.mAttrs, AttrStage::Pre, [&](const Span& sp, const auto& d, const auto& a) {
                                    d.handle(sp, a, es.wb, es.crate, si);
                                });
                                ExpandType(es, mod, si.mType);
                                ExpandExpr(es, si.defaultValue);
                                ExpandAttrs(es, si.mAttrs, AttrStage::Post, [&](const Span& sp, const auto& d, const auto& a) {
                                    d.handle(sp, a, es.wb, es.crate, si);
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
                        d.handle(sp, a, es.wb, es.crate, var); });
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
                ExpandGenericParams(es, mod, e.mParams);
                for (auto it = e.mVariants.begin(); it != e.mVariants.end();) {
                    auto& si = *it;
                    ExpandAttrsCfgAttr(*es.wb.settings, si.mAttrs);
                    ExpandAttrs(es, si.mAttrs, AttrStage::Pre, [&](const Span& sp, const auto& d, const auto& a) {
                        d.handle(sp, a, es.wb, es.crate, si);
                    });
                    ExpandType(es, mod, si.mType);
                    ExpandExpr(es, si.defaultValue);
                    ExpandAttrs(es, si.mAttrs, AttrStage::Post, [&](const Span& sp, const auto& d, const auto& a) {
                        d.handle(sp, a, es.wb, es.crate, si);
                    });

                    if (si.mName == "") {
                        it = e.mVariants.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
            TU_ARMA(Trait, e) {
                ExpandGenericParams(es, mod, e.params());
                for (auto& p : e.supertraits()) {
                    ExpandPath(es, mod, *p.ent.path);
                }
                auto& traitItems = e.items();
                for (size_t idx = 0; idx < traitItems.size(); idx++) {
                    auto& ti = traitItems[idx];
                    DEBUG(" - " << ti.name << " " << ti.data.tagStr());
                    auto attrs = mv$(ti.attrs);
                    auto tiPath = path + ti.name;
                    ExpandAttrsCfgAttr(*es.wb.settings, attrs);
                    ExpandAttrs(es, attrs, AttrStage::Pre, tiPath, mod, e, ti.data);

                TU_MATCH_HDRA( (ti.data), {)
                default:
                    BUG(Span(), "Unknown item type in trait block - " << ti.data.tagStr());
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
                                        traitItems.insert(traitItems.begin() + insertPos, mv$(i));
                                        insertPos++;
                                    }
                                    // - Any new macro invocations ends up at the end of the list and handled
                                    traitItems[idx].data = ASTItem::make_None({});
                                } else {
                                    // Move back in (using the index, as the old pointer may be invalid)
                                    traitItems[idx].data.as_MacroInv() = mv$(miOwned);
                                }
                            }
                        }
                        TU_ARMA(Function, e) {
                            ExpandFunction(es, mod, e);
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
                        auto& ti = traitItems[idx];

                        ExpandAttrs(es, attrs, AttrStage::Post, tiPath, mod, e, ti.data);
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
                ExpandFunction(es, mod, e);
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
            if (i.data.tag() == ASTItem::TAGDEAD) {
                i.data = mv$(dat);
            }
            // TODO: When would this _not_ be empty?
            if (i.attrs.mItems.size() == 0) {
                i.attrs = mv$(attrs);
            }
        }
    }

    // IGNORE m_anon_modules, handled as part of expressions
}

void ExpandModIndexAnon(ASTCrate& crate, ASTModule& mod) {
    TRACE_FUNCTION_F("mod=" << mod.path());

    for (auto& i : mod.mItems) {
        DEBUG("- " << i->data.tagStr() << " '" << i->name << "'");
        if (auto* e = i->data.opt_Module()) {
            ExpandModIndexAnon(crate, *e);

            // TODO: Also ensure that all #[macro_export] macros end up in parent
        }
    }

    for (auto& mp : mod.anonMods()) {
        if (mp.use_count() == 1) {
            DEBUG("- " << mp->path() << " dropped due to node destruction");
            mp.reset();
        } else {
            ExpandModIndexAnon(crate, *mp);
        }
    }
}

// Expand all `cfg` attributes... mostly to find #[macro_export]
void Expand_Mod_Early(const WireBoard& wb, ASTCrate& crate, ASTModule& mod, std::vector<std::unique_ptr<ASTNamed<ASTItem>>>& newRootItems) {
    TRACE_FUNCTION_F(mod.path());
    for (auto& i : mod.mItems) {
        if (const auto* mi = i->data.opt_MacroInv()) {
            if (mi->path().isTrivial() && mi->path().asTrivial() == "macro_rules") {
                i->vis = ASTVisibility::makeGlobal();
                DEBUG("macro_rules made pub");
            }
        }

        ExpandAttrsCfgAttr(*wb.settings, i->attrs);
        bool isMacroExport = false;
        bool cfgFailed = false;
        for (auto& a : i->attrs.mItems) {
            if (a.name() == "cfg") {
                if (!checkCfg(*wb.settings, i->span, a)) {
                    cfgFailed = true;
                }
            } else if (a.name() == "macro_export") {
                isMacroExport = true;
            } else {
            }
        }
        if (cfgFailed) {
            i->data = ASTItem::make_None({});
        } else if (isMacroExport) {
            if (i->data.is_MacroInv() && i->data.as_MacroInv().path().isTrivial() && i->data.as_MacroInv().path().asTrivial() == "macro_rules") {
                const auto& macInv = i->data.as_MacroInv();
                DEBUG("macro_rules marked with #[macro_export] moved to the crate root - " << macInv.inputIdent());
                newRootItems.push_back(box$(*i));
                i->data = ASTItem();

            } else if (i->data.is_Macro()) {
                // TODO: `#[macro_export] macro foo { ... }` DOESN'T move the item to the root
                // - Instead, it should add an alias? Or just tag for export
                DEBUG("macro item export: " << i->name);
                i->data.as_Macro()->exported = true;
            } else {
                ERROR(i->span, E0000, "#[macro_export] on non-macro_rules - " << i->data.tagStr());
            }
        } else if (auto* e = i->data.opt_Module()) {
            Expand_Mod_Early(wb, crate, *e, newRootItems);
        } else {
        }
    }

    DEBUG("Items");
    for (unsigned int idx = 0; idx < mod.mItems.size(); idx++) {
        auto& i = *mod.mItems[idx];
        if (auto* mi = i.data.opt_MacroInv()) {
            // 1.74 HACK - Parse `macro_rules` during the first pass, so they're present for `use` to refer to
            if (mi->path().isTrivial() && mi->path().asTrivial() == "macro_rules") {
                // A #[rustc_builtin_macro] declaration only supplies the
                // public signature of a compiler-provided macro.  Its
                // macro_rules body is a placeholder and must never enter the
                // ordinary macro namespace or shadow the builtin expander.
                if (i.attrs.get("rustc_builtin_macro")) {
                    continue;
                }
                auto miOwned = mv$(*mi);

                TRACE_FUNCTION_F("Macro invoke " << miOwned.path());

                ExpandState es{wb, crate, {}, ExpandMode::Iterate};
                auto ttl = ExpandMacro(es, mod, miOwned);
                ASSERT_BUG(miOwned.span(), ttl, "BUG: macro_rules not expanded");
                assert(miOwned.path().isValid());

                if (ttl.get()) {
                    // Re-parse tt
                    assert(ttl.get());
                    DEBUG("-- Parsing as mod items");
                    ParseModRootItemsInto(mod, idx, *ttl);

                } else {
                }
                mod.mItems[idx]->data.as_MacroInv() = mv$(miOwned);
            }
        }
    }
}

void Expand(const WireBoard& wb, ASTCrate& crate) {
    for (const auto& e : gDecorators) {
        DEBUG("Decorator: " << e.first);
    }
    for (const auto& e : gMacros) {
        DEBUG("Macro: " << e.first);
    }

    ExpandState es{wb, crate, LList<const ASTModule*>(nullptr, &crate.mRootModule), ExpandMode::FirstPass};

    // 1. Crate attributes
    ExpandAttrsCfgAttr(*es.wb.settings, crate.mAttrs);
    ExpandAttrs(es, crate.mAttrs, AttrStage::Pre, [&](const Span& sp, const auto& d, const auto& a) {
        d.handle(sp, a, es.wb, crate);
    });

    // TODO: Crate name and type

    std::vector<std::unique_ptr<ASTNamed<ASTItem>>> newRootItems;
    Expand_Mod_Early(wb, crate, crate.mRootModule, newRootItems);
    crate.mRootModule.mItems.insert(crate.mRootModule.mItems.begin(), std::make_move_iterator(newRootItems.begin()), std::make_move_iterator(newRootItems.end()));

    // Insert magic for libstd/libcore
    // NOTE: The actual crates are loaded in "LoadCrates" using magic in AST::Crate::load_externs
    RcString stdCrateShortname;
    RcString stdCrateName;
    switch (crate.loadStd) {
        case ASTCrate::LOAD_STD:
            stdCrateShortname = RcString::newInterned("std");
            stdCrateName = crate.extCratenameStd;
            break;
        case ASTCrate::LOAD_CORE:
            stdCrateShortname = RcString::newInterned("core");
            stdCrateName = crate.extCratenameCore;
            break;
        case ASTCrate::LOAD_NONE:
            break;
    }
    if (stdCrateShortname != "") {
        ASSERT_BUG(Span(), stdCrateName != "", "`" << stdCrateShortname << "` not loaded?");
        if (crate.preludePath == ASTPath()) {
            switch (crate.edition) {
                case ASTEdition::Rust2015:
                case ASTEdition::Rust2018:
                    crate.preludePath = ASTPath(stdCrateName, {ASTPathNode("prelude"), ASTPathNode("v1")});
                    break;
                case ASTEdition::Rust2021:
                    crate.preludePath = ASTPath(stdCrateName, {ASTPathNode("prelude"), ASTPathNode("rust_2021")});
                    break;
                case ASTEdition::Rust2024:
                    crate.preludePath = ASTPath(stdCrateName, {ASTPathNode("prelude"), ASTPathNode("rust_2024")});
                    break;
            }
        }
        ASTAttributeList attrs;
        ASTAttributeName name;
        name.elems.push_back("macro_use");
        attrs.push_back(ASTAttribute(Span(), mv$(name), {}));
        // NOTE: For `macro_use` we want to import this first, but for item lookup, we want it to be last.
        // - Solution, add to the end - but pre-visit the attributes
        crate.mRootModule.mItems.push_back(box$(ASTNamed<ASTItem>(Span(), mv$(attrs), ASTVisibility::makeRestricted(ASTVisibility::Ty::Private, ASTAbsolutePath()), stdCrateShortname, ASTItem::make_Crate({stdCrateName}))));
        auto& i = *crate.mRootModule.mItems.back();
        ExpandAttrs(es, i.attrs, AttrStage::Post, ASTAbsolutePath(), crate.mRootModule, 0, i.vis, i.data);
    }

    // 2. Module attributes
    for (auto& a : crate.mAttrs.mItems) {
        for (auto& d : gDecorators) {
            if (a.name() == d.first && d.second->stage() == AttrStage::Pre) {
            }
        }
    }

    // 3. Module tree
    // Loop until no more expansions happen
    // - Combine this with allowing macros to fail to expand, to be caught with a final pass
    ExpandMod(es, ASTAbsolutePath(), crate.mRootModule);
    DEBUG("(first) es.change = " << es.change << ", es.has_missing=" << es.hasMissing << " (" << &es << ")");
    if (es.hasMissing) {
        for (size_t nIters = 0; nIters < 5 && es.change && es.hasMissing; nIters++) {
            es.mode = ExpandMode::Iterate;
            es.change = false;
            es.hasMissing = false;
            ExpandMod(es, ASTAbsolutePath(), crate.mRootModule);
            DEBUG("?(Iter) es.change = " << es.change << ", es.has_missing=" << es.hasMissing);
        }
        es.hasMissing = false;
    }
    es.mode = ExpandMode::Final;
    ExpandMod(es, ASTAbsolutePath(), crate.mRootModule);
    ASSERT_BUG(Span(), !es.hasMissing, "Expand too too many attempts");

    // Post-process
    ExpandModIndexAnon(crate, crate.mRootModule);

    // Extract exported macros

    {
        auto& exportedMacros = crate.exportedMacros;

        ::std::vector<ASTModule*> mods;
        mods.push_back(&crate.mRootModule);
        do {
            auto& mod = *mods.back();
            mods.pop_back();

            for (/*const*/ auto& mac : mod.macros()) {
                if (mac.data->exported) {
                    auto res = exportedMacros.insert(::std::make_pair(mac.name, &*mac.data));
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
        for (const auto& mac : crate.mRootModule.macroImports) {
            if (mac.isPub) {
                if (!mac.ref.is_MacroRules()) {
                    continue;
                }
                auto v = ::std::make_pair(mac.name, mac.ref.as_MacroRules());

                auto it = exportedMacros.find(mac.name);
                if (it == exportedMacros.end()) {
                    auto res = exportedMacros.insert(mv$(v));
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
