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

// Per-attribute action for ExpandAttrs/ExpandAttr. Call sites keep their
// lambdas via makeCallable<ExpandAttrCb>(...); the API sees only this
// interface, with no allocation.
struct ExpandAttrCallback {
    virtual void run(const Span& sp, const ExpandDecorator& d, const ASTAttribute& a) const = 0;
};

template <typename F>
struct ExpandAttrCb final: ExpandAttrCallback {
    F f;

    explicit ExpandAttrCb(F f)
        : f(f)
    {
    }

    void run(const Span& sp, const ExpandDecorator& d, const ASTAttribute& a) const override {
        f(sp, d, a);
    }
};

void ExpandAttrs(const ExpandState& es, const ASTAttributeList& attrs, AttrStage stage, const ExpandAttrCallback& f);
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
    auto oldItems = std::move(mod.items);
    // Parse module items
    ParseModRootItems(lex, mod);
    // Then insert the newly created items
    oldItems.insert(oldItems.begin() + idx + 1, std::make_move_iterator(mod.items.begin()), std::make_move_iterator(mod.items.end()));
    // and move the (updated) item list back in
    mod.items = std::move(oldItems);
}

void ExpandAttr(const ExpandState& es, const Span& sp, const ASTAttribute& a, AttrStage stage, const ExpandAttrCallback& f) {
    bool found = false;
    if (a.name().elems.empty()) {
        return;
    }
    if (a.isInert()) {
        return;
    }
    DEBUG(a);
    // Run a built-in attribute, if this pass is the one it belongs to.
    auto runDecorator = [&](const RcString& name, const ExpandDecorator& d) {
        if (d.stage() != stage) {
            DEBUG("#[" << name << "] Ignore: Wrong stage " << (int)d.stage() << " != " << (int)stage);
            return;
        }
        if (!d.runDuringIter()) {
            switch (es.mode) {
                case ExpandMode::FirstPass:
                case ExpandMode::Iterate:
                    if (stage != AttrStage::Pre) {
                        DEBUG("#[" << name << "] m=" << (int)es.mode);
                        return;
                    }
                    break;
                case ExpandMode::Final:
                    if (stage != AttrStage::Post) {
                        DEBUG("#[" << name << "] m=" << (int)es.mode);
                        return;
                    }
                    break;
            }
        }
        DEBUG("#[" << name << "]");
        f.run(sp, d, a);
        // Annotate the attribute as having been handled
        a.markInert();
    };
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
                f.run(sp, *d.second, a);
                // Annotate the attribute as having been handled
                a.markInert();
            }
        }
    }
    if (!found) {
        // The attribute may have been imported under another name
        // (`use derive as my_derive;`). The index that records that is built
        // after expansion, so read the `use` items themselves for the name a
        // built-in answers to.
        if (a.name().elems.size() == 1) {
            const auto& want = a.name().elems[0];
            for (const auto* ll = &es.modstack; ll && !found; ll = ll->prev) {
                if (!ll->item) {
                    continue;
                }
                for (const auto& i : ll->item->items) {
                    const auto* u = i->data.opt_Use();
                    if (!u) {
                        continue;
                    }
                    for (const auto& ent : u->entries) {
                        if (ent.name != want) {
                            continue;
                        }
                        RcString target;
                        if (ent.path.cls.is_Local()) {
                            target = ent.path.cls.as_Local().name;
                        } else if (!ent.path.nodes().empty()) {
                            target = ent.path.nodes().back().name();
                        }
                        if (target != RcString()) {
                            if (const auto* d = ExpandFindDecorator(target)) {
                                found = true;
                                runDecorator(target, *d);
                            }
                        }
                        break;
                    }
                    if (found) {
                        break;
                    }
                }
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
                            //   > This seems to so the derive (first attribute) can see the trait list? (does trustme handle that properly? I think so)
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
                f.run(sp, d, a);
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

void ExpandAttrs(const ExpandState& es, const ASTAttributeList& attrs, AttrStage stage, const ExpandAttrCallback& f) {
    // Reduce load on derive etc by visiting `cfg` first.
    for (auto& a : attrs.items) {
        static const RcString rcstringCfg = RcString::newInterned("cfg");
        if (!a.isInert() && a.name() == rcstringCfg) {
            ExpandAttr(es, a.span(), a, stage, f);
        }
    }
    for (auto& a : attrs.items) {
        ExpandAttr(es, a.span(), a, stage, f);
    }
}

void ExpandAttrsCfgAttr(const Settings& settings, ASTAttributeList& attrs) {
    for (auto it = attrs.items.begin(); it != attrs.items.end();) {
        auto& a = *it;
        static const RcString rcstringCfgAttr = RcString::newInterned("cfg_attr");
        if (a.name() == rcstringCfgAttr) {
            auto newAttrs = checkCfgAttr(settings, a);
            it = attrs.items.erase(it);
            it = attrs.items.insert(it, std::make_move_iterator(newAttrs.begin()), std::make_move_iterator(newAttrs.end()));
        } else {
            ++it;
        }
    }
}

namespace {
    slice<const ASTAttribute> getAttrsAfter(const ASTAttributeList& attrs, const ASTAttribute& a) {
        const auto* start = &a + 1;
        const auto* end = &attrs.items.back() + 1;
        return slice<const ASTAttribute>(start, end - start);
    }
}

void ExpandAttrs(const ExpandState& es, const ASTAttributeList& attrs, AttrStage stage, const ASTAbsolutePath& path, ASTModule& mod, size_t modIdx, const ASTVisibility& vis, ASTItem& item) {
    ExpandAttrs(es, attrs, stage, makeCallable<ExpandAttrCb>([&](const Span& sp, const ExpandDecorator& d, const ASTAttribute& a) {
        if (!item.is_None()) {
            // Pass attributes _after_ this attribute (or all of them, if the decorator asks)
            auto attrsSlice = d.wantsAllAttrs() ? slice<const ASTAttribute>(attrs.items.data(), attrs.items.size()) : getAttrsAfter(attrs, a);
            d.handle(sp, a, es.wb, es.crate, path, mod, modIdx, attrsSlice, vis, item);
        }
    }));
}

void ExpandAttrs(const ExpandState& es, const ASTAttributeList& attrs, AttrStage stage, const ASTAbsolutePath& path, ASTModule& mod, ASTTrait& trait, ASTItem& item) {
    gCurrentMod = &mod;
    ExpandAttrs(es, attrs, stage, makeCallable<ExpandAttrCb>([&](const Span& sp, const ExpandDecorator& d, const ASTAttribute& a) {
        if (!item.is_None()) {
            d.handle(sp, a, es.wb, es.crate, path, trait, getAttrsAfter(attrs, a), item);
        }
    }));
    gCurrentMod = nullptr;
}

void ExpandAttrs(const ExpandState& es, const ASTAttributeList& attrs, AttrStage stage, ASTModule& mod, ASTImpl& impl, const ASTVisibility& vis, const RcString& name, ASTItem& item) {
    gCurrentMod = &mod;
    ExpandAttrs(es, attrs, stage, makeCallable<ExpandAttrCb>([&](const Span& sp, const ExpandDecorator& d, const ASTAttribute& a) {
        if (!item.is_None()) {
            d.handle(sp, a, es.wb, es.crate, impl, name, getAttrsAfter(attrs, a), vis, item);
        }
    }));
    gCurrentMod = nullptr;
}

bool ExpandAttrsCfgOnly(const ExpandState& es, ASTAttributeList& attrs) {
    bool remove = false;
    ExpandAttrsCfgAttr(*es.wb.settings, attrs);
    ExpandAttrs(es, attrs, AttrStage::Pre, makeCallable<ExpandAttrCb>([&](const Span& sp, const ExpandDecorator& d, const ASTAttribute& a) {
        if (a.name() == "cfg") {
            if (!checkCfg(*es.wb.settings, sp, a)) {
                remove = true;
            }
            return;
        } else if (a.name() == "allow" || a.name() == "warn" || a.name() == "deny" || a.name() == "forbid" || a.name() == "expect") {
            // A lint level on a parameter changes nothing here.
        } else if (a.name() == "doc") {
            // Documentation is not code.
        } else {
            TODO(sp, "non-cfg attributes - " << a);
        }
    }));
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

    const bool hasDefinitionModule = path.cls.is_Relative() && path.cls.as_Relative().hygiene.hasModPath();
    if (path.isTrivial() && !hasDefinitionModule) {
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
                    if (!mr.data->exported && mr.data->definitionSpan && miSpan) {
                        const auto& definition = mr.data->definitionSpan.getTopFileSpan();
                        const auto& invocation = miSpan.getTopFileSpan();
                        if (definition.filename == invocation.filename
                            && (definition.startLine > invocation.startLine
                                || (definition.startLine == invocation.startLine && definition.startOfs > invocation.startOfs))) {
                            DEBUG(macMod.path() << "::" << mr.name << " - Defined later, skipping");
                            continue;
                        }
                    }
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
    switch (rv.tag()) {
        case ResolveItemRefMacro::TAG_None: {
            return MacroRef();
        }
        case ResolveItemRefMacro::TAG_InternalMacro: {
            auto& pm = rv.as_InternalMacro();
            return pm;
        }
        case ResolveItemRefMacro::TAG_ProcMacro: {
            auto& pm = rv.as_ProcMacro();
            return pm;
        }
        case ResolveItemRefMacro::TAG_MacroRules: {
            auto& p = rv.as_MacroRules();
            return p;
        }
    }
    return MacroRef();
}

::std::unique_ptr<TokenStream> ExpandMacroInner(const WireBoard& wb, const ASTCrate& crate, LList<const ASTModule*> modstack, ASTModule& mod, Span miSpan, const ASTPath& path, const RcString& inputIdent, TokenTree& inputTt) {
    ASSERT_BUG(miSpan, path.isValid(), "Macro invocation with invalid path");

    TRACE_FUNCTION_F("Searching for macro " << path);

    // Find the macro. `macro_rules! NAME { ... }` is always a definition, even
    // where a user macro named `macro_rules` is in scope -- only the identifier
    // tells the two apart.
    MacroRef mac;
    if (inputIdent != "" && path.isTrivial() && path.asTrivial() == "macro_rules") {
        if (auto* pm = ExpandFindProcMacro(path.asTrivial())) {
            mac = MacroRef(pm);
        }
    }
    if (mac.is_None()) {
        mac = ExpandLookupMacro(miSpan, wb, crate, modstack, path);
    }
    if (mac.is_MacroRules()) {
        // TODO: If `mr_ptr` is tagged with #[rustc_builtin_macro], look for a matching entry in `g_macros`
    }

    ::std::unique_ptr<TokenStream> rv;
    switch (mac.tag()) {
        case MacroRef::TAG_None: {
            DEBUG("Unknown macro " << path);
            return ::std::unique_ptr<TokenStream>();
        }
        case MacroRef::TAG_ExternalProcMacro: {
            auto& procMac = mac.as_ExternalProcMacro();
            ::std::vector<RcString> macPath;
            macPath.push_back(procMac->path.crateName());
            macPath.insert(macPath.end(), procMac->path.components().begin(), procMac->path.components().end());
            rv = ProcMacroInvoke(miSpan, wb, crate, macPath, inputTt);
            break;
        }
        case MacroRef::TAG_BuiltinProcMacro: {
            auto& procMac = mac.as_BuiltinProcMacro();
            ASSERT_BUG(miSpan, procMac, "null BuiltinProcMacro? " << path);
            rv = inputIdent == "" ? procMac->expand(miSpan, wb, crate, inputTt, mod) : procMac->expandIdent(miSpan, wb, crate, inputIdent, inputTt, mod);
            break;
        }
        case MacroRef::TAG_MacroRules: {
            auto& mrPtr = mac.as_MacroRules();
            if (inputIdent != "") {
                ERROR(miSpan, E0000, "macro_rules! macros can't take an ident");
            }

            DEBUG("Invoking macro_rules " << path << " " << mrPtr);
            rv = MacroInvokeRules(path.isTrivial() ? path.asTrivial() : RcString::newInterned(FMT(path).c_str()), *mrPtr, miSpan, wb, mv$(inputTt), crate, mod);
            inputTt = TokenTree();
            break;
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
    switch (pat.data().tag()) {
        case ASTPatternData::TAG_MaybeBind: {
            break;
        }
        case ASTPatternData::TAG_Never: {
            break;
        }
        case ASTPatternData::TAG_Macro: {
            auto& e = pat.data().as_Macro();
            const auto span = e.inv->span();

            auto tt = ExpandMacro(es, mod, *e.inv);
            if (tt) {
                ASTPattern newpat;
                if (tt->isMacroExpansionPlaceholder() && tt->lookahead(0) == TOK_EOF) {
                    newpat = ASTPattern(span, ASTPattern::Data::make_Any({}));
                } else {
                    auto& lex = *tt;
                    newpat = ParsePattern(lex);
                    if (LOOK_AHEAD(lex) != TOK_EOF) {
                        ERROR(span, E0000, "Trailing tokens in macro expansion");
                    }
                }

                for (auto& b : pat.bindings()) {
                    newpat.bindings().push_back(std::move(b));
                }

                pat = mv$(newpat);
                ExpandPattern(es, mod, pat, isRefutable);
            }
            break;
        }
        case ASTPatternData::TAG_Any: {
            break;
        }
        case ASTPatternData::TAG_Box: {
            auto& e = pat.data().as_Box();
            ExpandPattern(es, mod, *e.sub, isRefutable);
            break;
        }
        case ASTPatternData::TAG_Guard: {
            auto& e = pat.data().as_Guard();
            ExpandPattern(es, mod, *e.sub, isRefutable);
            break;
        }
        case ASTPatternData::TAG_Deref: {
            auto& e = pat.data().as_Deref();
            ExpandPattern(es, mod, *e.sub, isRefutable);
            break;
        }
        case ASTPatternData::TAG_Ref: {
            auto& e = pat.data().as_Ref();
            ExpandPattern(es, mod, *e.sub, isRefutable);
            break;
        }
        case ASTPatternData::TAG_Value: {
            break;
        }
        case ASTPatternData::TAG_ValueLeftInc: {
            break;
        }
        case ASTPatternData::TAG_Tuple: {
            auto& e = pat.data().as_Tuple();
            for (auto& sp : e.start) {
                ExpandPattern(es, mod, sp, isRefutable);
            }
            for (auto& sp : e.end) {
                ExpandPattern(es, mod, sp, isRefutable);
            }
            break;
        }
        case ASTPatternData::TAG_StructTuple: {
            auto& e = pat.data().as_StructTuple();
            for (auto& sp : e.tupPat.start) {
                ExpandPattern(es, mod, sp, isRefutable);
            }
            for (auto& sp : e.tupPat.end) {
                ExpandPattern(es, mod, sp, isRefutable);
            }
            break;
        }
        case ASTPatternData::TAG_Struct: {
            auto& e = pat.data().as_Struct();
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
            break;
        }
        case ASTPatternData::TAG_Slice: {
            auto& e = pat.data().as_Slice();
            for (auto& sp : e.subPats) {
                ExpandPattern(es, mod, sp, isRefutable);
            }
            break;
        }
        case ASTPatternData::TAG_SplitSlice: {
            auto& e = pat.data().as_SplitSlice();
            for (auto& sp : e.leading) {
                ExpandPattern(es, mod, sp, isRefutable);
            }
            for (auto& sp : e.trailing) {
                ExpandPattern(es, mod, sp, isRefutable);
            }
            break;
        }
        case ASTPatternData::TAG_Or: {
            auto& e = pat.data().as_Or();
            for (auto& sp : e) {
                ExpandPattern(es, mod, sp, isRefutable);
            }
            break;
        }
    }
}

/// `async Fn(..)` is the async callable trait of the same shape, whose path
/// only becomes writable once the core crate is known.
static void ExpandAsyncCallableTrait(const ExpandState& es, TypeTraitPath& tp) {
    if (!tp.isAsync) {
        return;
    }
    const auto& name = tp.path->nodes().back().name();
    const char* replacement = nullptr;
    if (name == "Fn") {
        replacement = "AsyncFn";
    } else if (name == "FnMut") {
        replacement = "AsyncFnMut";
    } else if (name == "FnOnce") {
        replacement = "AsyncFnOnce";
    } else {
        ERROR(Span(), E0000, "`async` is only valid on the callable traits, not " << *tp.path);
    }
    auto args = mv$(tp.path->nodes().back().args());
    auto path = ASTPath(ASTAbsolutePath(es.crate.extCratenameCore, {RcString::newInterned("ops"), RcString::newInterned(replacement)}));
    path.nodes().back().args() = mv$(args);
    *tp.path = mv$(path);
    tp.isAsync = false;
}

void ExpandType(const ExpandState& es, ASTModule& mod, ::ASTType*& ty) {
    switch (ty->data.tag()) {
        case TypeData::TAG_None: {
            break;
        }
        case TypeData::TAG_Any: {
            break;
        }
        case TypeData::TAG_Unit: {
            break;
        }
        case TypeData::TAG_Bang: {
            break;
        }
        case TypeData::TAG_Macro: {
            auto& e = ty->data.as_Macro();
            auto tt = ExpandMacro(es, mod, *e.inv);
            if (tt) {
                ASTType* newTy;
                if (tt->isMacroExpansionPlaceholder() && tt->lookahead(0) == TOK_EOF) {
                    newTy = mkType(*es.crate.pool, ASTTypeTags::Unit(), e.inv->span());
                } else {
                    newTy = ParseType(*tt);
                    if (tt->lookahead(0) != TOK_EOF) {
                        ERROR(e.inv->span(), E0000, "Extra tokens after parsed type");
                    }
                }
                ty = mv$(newTy);

                ExpandType(es, mod, ty);
            }
            break;
        }
        case TypeData::TAG_Primitive: {
            break;
        }
        case TypeData::TAG_Function: {
            auto& e = ty->data.as_Function();
            TypeFunction& tf = e.info;
            ExpandType(es, mod, tf.rettype);
            for (auto& st : tf.argTypes) {
                ExpandType(es, mod, st);
            }
            break;
        }
        case TypeData::TAG_Tuple: {
            auto& e = ty->data.as_Tuple();
            for (auto& st : e.innerTypes) {
                ExpandType(es, mod, st);
            }
            break;
        }
        case TypeData::TAG_Borrow: {
            auto& e = ty->data.as_Borrow();
            ExpandType(es, mod, e.inner);
            if (e.isPin) {
                // `&pin mut T` is `Pin<&mut T>`; the core crate is known now.
                auto reference = mkType(*es.crate.pool, ASTTypeTags::Reference(), ty->span(), e.lifetime, e.isMut, e.inner);
                auto path = ASTPath(ASTAbsolutePath(es.crate.extCratenameCore, {RcString::newInterned("pin"), RcString::newInterned("Pin")}));
                path.nodes().back().args().entries.push_back(reference);
                ty = mkType(*es.crate.pool, ASTTypeTags::Path(), ty->span(), mv$(path));
            }
            break;
        }
        case TypeData::TAG_Pointer: {
            auto& e = ty->data.as_Pointer();
            ExpandType(es, mod, e.inner);
            break;
        }
        case TypeData::TAG_Array: {
            auto& e = ty->data.as_Array();
            ExpandType(es, mod, e.inner);
            if (e.size) {
                ExpandExpr(es, e.size);
            }
            break;
        }
        case TypeData::TAG_Slice: {
            auto& e = ty->data.as_Slice();
            ExpandType(es, mod, e.inner);
            break;
        }
        case TypeData::TAG_Pattern: {
            auto& e = ty->data.as_Pattern();
            ExpandType(es, mod, e.inner);
            ExpandPattern(es, mod, *e.pattern, true);
            break;
        }
        case TypeData::TAG_Generic: {
            break;
        }
        case TypeData::TAG_Path: {
            auto& e = ty->data.as_Path();
            ExpandPath(es, mod, *e);
            break;
        }
        case TypeData::TAG_TraitObject: {
            auto& e = ty->data.as_TraitObject();
            for (auto& p : e.traits) {
                // TODO: p.hrbs? Not needed until types are in those
                ExpandAsyncCallableTrait(es, p);
                ExpandPath(es, mod, *p.path);
            }
            break;
        }
        case TypeData::TAG_ErasedType: {
            auto& e = ty->data.as_ErasedType();
            for (auto& p : e->traits) {
                // TODO: p.hrbs?
                ExpandAsyncCallableTrait(es, p);
                ExpandPath(es, mod, *p.path);
            }
            for (auto& p : e->maybeTraits) {
                // TODO: p.hrbs?
                ExpandPath(es, mod, *p.path);
            }
            if (e->use) {
                ExpandPathParams(es, mod, *e->use);
            }
            break;
        }
    }
}

void ExpandPathParams(const ExpandState& es, ASTModule& mod, ASTPathParams& params) {
    for (auto& e : params.entries) {
        switch (e.tag()) {
            case ASTPathParamEnt::TAG_Null: {
                auto& _ = e.as_Null();
                break;
            }
            case ASTPathParamEnt::TAG_Lifetime: {
                auto& _ = e.as_Lifetime();
                break;
            }
            case ASTPathParamEnt::TAG_Type: {
                auto& typ = e.as_Type();
                ExpandType(es, mod, typ);
                break;
            }
            case ASTPathParamEnt::TAG_Value: {
                auto& node = e.as_Value();
                ExpandExpr(es, node);
                break;
            }
            case ASTPathParamEnt::TAG_AssociatedTyEqual: {
                auto& aty = e.as_AssociatedTyEqual();
                ExpandPathParams(es, mod, aty.first.args());
                ExpandType(es, mod, aty.second);
                break;
            }
            case ASTPathParamEnt::TAG_AssociatedValueEqual: {
                auto& value = e.as_AssociatedValueEqual();
                ExpandPathParams(es, mod, value.first.args());
                ExpandExpr(es, value.second);
                break;
            }
            case ASTPathParamEnt::TAG_AssociatedTyBound: {
                auto& aty = e.as_AssociatedTyBound();
                ExpandPathParams(es, mod, aty.first.args());
                for (auto& p : aty.second) {
                    ExpandPath(es, mod, *p.path);
                }
                break;
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

    switch (p.cls.tag()) {
        case ASTPathClass::TAG_Invalid: {
            break;
        }
        case ASTPathClass::TAG_Local: {
            break;
        }
        case ASTPathClass::TAG_Relative: {
            auto& pe = p.cls.as_Relative();
            expandNodes(pe.nodes);
            break;
        }
        case ASTPathClass::TAG_Self: {
            auto& pe = p.cls.as_Self();
            expandNodes(pe.nodes);
            break;
        }
        case ASTPathClass::TAG_Super: {
            auto& pe = p.cls.as_Super();
            expandNodes(pe.nodes);
            break;
        }
        case ASTPathClass::TAG_Absolute: {
            auto& pe = p.cls.as_Absolute();
            expandNodes(pe.nodes);
            break;
        }
        case ASTPathClass::TAG_UFCS: {
            auto& pe = p.cls.as_UFCS();
            ExpandType(es, mod, pe.type);
            if (pe.trait) {
                ExpandPath(es, mod, *pe.trait);
            }
            expandNodes(pe.nodes);
            break;
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
    ::std::vector<RcString> tryStack;
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
            ExpandAttrs(expandState, attrs, AttrStage::Pre, makeCallable<ExpandAttrCb>([&](const Span& sp, const ExpandDecorator& d, const auto& a) {
                d.handle(sp, a, this->expandState.wb, this->crate, cnode);
            }));
            if (cnode.get()) {
                cnode->attrs() = mv$(attrs);
            }
        }
        if (cnode.get()) {
            cnode->visit(*this);
            // If the node was a macro, and it was consumed, reset it
            if (auto* nMac = cast<ASTExprNodeMacro>(cnode.get())) {
                if (!nMac->path.isValid()) {
                    cnode.reset();
                }
            }
            if (this->replacement) {
                cnode = mv$(this->replacement);
            }
        }

        if (cnode.get()) {
            auto attrs = mv$(cnode->attrs());
            ExpandAttrs(expandState, attrs, AttrStage::Post, makeCallable<ExpandAttrCb>([&](const Span& sp, const auto& d, const auto& a) {
                d.handle(sp, a, this->expandState.wb, this->crate, cnode);
            }));
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
        TRACE_FUNCTION_F(node.path << "!");
        if (!node.path.isValid()) {
            return ASTExprNodeP();
        }

        const bool definesMacro = node.path.isTrivial() && node.path.asTrivial() == "macro_rules";

        // How deep this invocation already is: each expansion adds a frame to
        // the span naming the macro that produced the tokens.
        unsigned int depth = 0;
        for (Span frame = node.span(); frame; frame = frame->parentSpan) {
            if (cast<const SpanInnerMacro>(frame.get())) {
                depth++;
            }
        }
        if (depth >= expandState.wb.settings->recursionLimit) {
            ERROR(node.span(), E0000, "recursion limit reached while expanding `" << node.path << "!`");
        }

        ASTExprNodeP rv;
        auto& mod = this->curMod();
        auto ttl = ExpandMacro(expandState, mod, node.span(), node.path, node.ident, node.tokens);
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
                    auto marker = ASTExprNodeP(new ASTExprNodeMacroDefinition(it->data->definitionId, it->data->hygiene, it->data->definitionHygiene));
                    marker->setSpan(node.span());
                    nodesOut->push_back({true, ::std::move(marker)});
                }
            }
            if (!nodesOut) {
                if (ttl->lookahead(0) != TOK_EOF) {
                    SET_MODULE((*ttl), mod);
                    DEBUG("-- Parsing as expression");
                    rv = ParseExpr0(*ttl);
                    // A macro body written as `expr;` and used where an
                    // expression is expected keeps the expression: rustc reports
                    // the trailing semicolon as a lint, not an error.
                    if (ttl->lookahead(0) == TOK_SEMICOLON && ttl->lookahead(1) == TOK_EOF) {
                        ttl->getToken();
                    }
                    if (ttl->lookahead(0) != TOK_EOF) {
                        ERROR(node.span(), E0000, "Unused tokens at the end of macro expansion - " << ttl->getToken());
                    }
                } else if (ttl->isMacroExpansionPlaceholder()) {
                    rv = ASTExprNodeP(new ASTExprNodeTuple({}));
                    rv->setSpan(node.span());
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
            node.path = ASTPath();

            if (!nodesOut && !rv) {
                ERROR(node.span(), E0000, "Macro didn't expand to anything");
            }
        }

        return mv$(rv);
    }

    void visit(ASTExprNodeMacro& node) override {
        TRACE_FUNCTION_F("ExprNode_Macro - name = " << node.path);
        if (!node.path.isValid()) {
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
        bool hasLocalMod = node.localMod != nullptr;

        auto prevModstack = this->expandState.modstack;
        if (node.localMod) {
            this->expandState.modstack = LList<const ASTModule*>(&prevModstack, node.localMod.get());
        }

        // TODO: macro_rules! invocations within the expression list influence this.
        // > Solution: Defer creation of the local module until during expand.
        if (node.localMod) {
            ExpandMod(this->expandState, node.localMod->path(), *node.localMod);
            modItemCount = node.localMod->items.size();
        }

        auto saved = this->currentBlock;
        this->currentBlock = &node;

        for (auto it = node.nodes.begin(); it != node.nodes.end();) {
            assert(it->node.get());

            if (auto* nodeMac = cast<ASTExprNodeMacro>(it->node.get())) {
                const bool definesMacro = nodeMac->path.isTrivial() && nodeMac->path.asTrivial() == "macro_rules";
                const auto macroName = nodeMac->ident;
                auto attrs = std::move(it->node->attrs());
                ExpandAttrsCfgAttr(*expandState.wb.settings, attrs);
                ExpandAttrs(expandState, attrs, AttrStage::Pre, makeCallable<ExpandAttrCb>([&](const Span& sp, const auto& d, const auto& a) {
                    d.handle(sp, a, this->expandState.wb, this->crate, it->node);
                }));
                if (!it->node.get()) {
                    it = node.nodes.erase(it);
                    continue;
                }
                it->node->attrs() = std::move(attrs);

                assert(it->node.get() == nodeMac);

                ::std::vector<ASTExprNodeBlock::Line> newNodes;
                this->visitMacro(*nodeMac, &newNodes);
                if (!hasLocalMod && node.localMod) {
                    this->expandState.modstack = LList<const ASTModule*>(&prevModstack, node.localMod.get());
                    hasLocalMod = true;
                }
                if (node.localMod && modItemCount < node.localMod->items.size()) {
                    ExpandMod(this->expandState, node.localMod->path(), *node.localMod, modItemCount);
                    modItemCount = node.localMod->items.size();
                }
                if (definesMacro && !nodeMac->path.isValid()) {
                    for (auto& attr : nodeMac->attrs().items) {
                        if (!attr.isInert() && attr.name() == "macro_export") {
                            ExpandExportMacroRules(attr.span(), attr, this->expandState.wb, this->crate, this->curMod(), macroName);
                            attr.markInert();
                        }
                    }
                }
                for (const auto& n : newNodes) {
                    DEBUG("++ " << *n.node << (n.hasSemicolon ? " ;" : ""));
                }

                if (nodeMac->path.isValid()) {
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
        ExpandType(this->expandState, this->curMod(), node.returnType);
        this->visitNodelete(node, node.inner);
    }

    void visit(ASTExprNodeTry& node) override {
        // Macro lookup can be deferred to a later expansion pass (for example,
        // through a glob import).  Keep the lexical try scope until macro
        // expansion has stabilised, so a `?` supplied as a macro argument is
        // not lowered as a return from the surrounding function.
        if (expandState.mode != ExpandMode::Final) {
            this->visitNodelete(node, node.inner);
            return;
        }

        // Desugar into
        // ```
        // loop '#tryNNN {
        //   break '#tryNNN { ... }
        // }
        // ```
        // NOTE: MIR lowering and HIR typecheck need to know to skip these (OR resolve should handle naming all loop blocks)
        tryStack.push_back(RcString::newInterned(FMT("#try" << tryIndex++)));
        this->visitNodelete(node, node.inner);
        auto loopName = mv$(tryStack.back());
        tryStack.pop_back();

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
        for (auto& v : node.params) {
            switch (v.tag()) {
                case ASTAsmParam::TAG_Const: {
                    auto& e = v.as_Const();
                    this->visitNodelete(node, e);
                    break;
                }
                case ASTAsmParam::TAG_Sym: {
                    auto& e = v.as_Sym();
                    ExpandPath(this->expandState, this->curMod(), e);
                    break;
                }
                case ASTAsmParam::TAG_Label: {
                    auto& e = v.as_Label();
                    this->visitNodelete(node, e.code);
                    break;
                }
                case ASTAsmParam::TAG_RegSingle: {
                    auto& e = v.as_RegSingle();
                    this->visitNodelete(node, e.val);
                    break;
                }
                case ASTAsmParam::TAG_Reg: {
                    auto& e = v.as_Reg();
                    this->visitNodelete(node, e.valIn);
                    this->visitNodelete(node, e.valOut);
                    break;
                }
            }
        }
    }

    void visit(ASTExprNodeFlow& node) override {
        this->visitNodelete(node, node.value);

        if (node.type == ASTExprNodeFlow::YEET) {
            if (expandState.mode != ExpandMode::Final) {
                return;
            }

            auto coreCrate = crate.extCratenameCore;
            auto pathOpsYeet = getPath(coreCrate, "ops", "Yeet");
            auto pathFromResidualFromResidual = getPath(coreCrate, "ops", "FromResidual", "from_residual");

            // `do yeet` with no value yeets a unit, the same as `do yeet ()`.
            auto yeeted = std::move(node.value);
            if (!yeeted) {
                yeeted = ASTExprNodeP(new ASTExprNodeTuple(::std::vector<ASTExprNodeP>()));
                yeeted->setSpan(node.span());
            }
            auto v = ASTExprNodeP(new ASTExprNodeCallPath(ASTPath(pathOpsYeet), ::makeVec1(std::move(yeeted))));
            v->setSpan(node.span());
            v = ASTExprNodeP(new ASTExprNodeCallPath(ASTPath(pathFromResidualFromResidual), ::makeVec1(std::move(v))));
            v->setSpan(node.span());
            replacement = ASTExprNodeP(new ASTExprNodeFlow(
                (tryStack.empty() ? ASTExprNodeFlow::RETURN : ASTExprNodeFlow::BREAK), // NOTE: uses `break 'tryblock` instead of return if in a try block.
                (tryStack.empty() ? RcString("") : tryStack.back()),
                std::move(v)
            ));
            replacement->setSpan(node.span());
        }
    }

    void visit(ASTExprNodeLetBinding& node) override {
        ExpandType(this->expandState, this->curMod(), node.type);
        ExpandPattern(this->expandState, this->curMod(), node.pat, false);
        this->visitNodelete(node, node.value);
        this->visitNodelete(node, node.elseNode);
    }

    void visit(ASTExprNodeAssign& node) override {
        inAssignLhs = true;
        this->visitNodelete(node, node.slot);
        inAssignLhs = false;
        this->visitNodelete(node, node.value);

        // Desugar destructuring assignment
        // https://rust-lang.github.io/rfcs/2909-destructuring-assignment.html
        if (node.op == ASTExprNodeAssign::NONE) {
            struct VisitorToPat: public ASTNodeVisitor {
                std::vector<std::pair<RcString, ASTExprNodeP>> slots;
                ASTPattern rv;

                bool rvSet = false;
                bool isSlot = false;

                ASTPattern lower(ASTExprNodeP& ep) {
                    assert(ep);
                    ep->visit(*this);
                    ASSERT_BUG(ep->span(), rvSet, ep.typeName() << " - Didn't yield a pattern");
                    if (isSlot) {
                        assert(!slots.empty());
                        assert(!slots.back().second);
                        slots.back().second = std::move(ep);
                        isSlot = false;
                    }
                    rvSet = false;
                    return std::move(rv);
                }

                // - This is a de-structuring pattern
                void pat(ASTPattern rv) {
                    assert(!rvSet);
                    assert(!isSlot);
                    rvSet = true;
                    assert(rv.bindings().empty());
                    this->rv = std::move(rv);
                }

                // - This is a slot (to be assigned)
                void slot(ASTExprNode& v) {
                    rvSet = true;
                    isSlot = true;

                    RcString name(FMT("_#" << slots.size()).c_str());
                    slots.push_back(std::make_pair(name, ASTExprNodeP()));
                    rv = ASTPattern(ASTPattern::TagBind(), v.span(), slots.back().first);
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

                // `..` in a destructuring position parses as a range with no
                // bounds, which is how the tuple, slice and tuple-struct forms
                // all spell "the rest".
                static bool isRest(ASTExprNodeP& ep) {
                    if (const auto* e = cast<ASTExprNodeBinOp>(ep.get())) {
                        return e->type == ASTExprNodeBinOp::RANGE && !e->left && !e->right && !e->parenthesised;
                    }
                    return false;
                }

                ASTPattern::TuplePat lowerTuplePat(const Span& sp, ::std::vector<ASTExprNodeP>& values) {
                    bool isSplit = false;
                    ::std::vector<ASTPattern> start;
                    ::std::vector<ASTPattern> end;
                    for (auto& m : values) {
                        if (isRest(m)) {
                            ASSERT_BUG(sp, !isSplit, "Multiple `..` in tuple pattern?");
                            isSplit = true;
                            start = std::move(end);
                            continue;
                        }
                        end.push_back(lower(m));
                    }
                    if (!isSplit) {
                        return ASTPattern::TuplePat{std::move(end), false, {}};
                    }
                    return ASTPattern::TuplePat{std::move(start), true, std::move(end)};
                }

                void visit(ASTExprNodeCallPath& v) override {
                    // `TupleStruct(a, b) = value` destructures through the
                    // tuple-struct (or enum variant) of that name.
                    pat(ASTPattern(ASTPattern::TagNamedTuple(), v.span(), v.path, lowerTuplePat(v.span(), v.args)));
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
                    rvSet = true;
                    rv = ASTPattern(v.span(), ASTPattern::Data::make_Any({}));
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

                void visit(ASTExprNodeSuffixedLiteral& v) override {
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
                    pat(ASTPattern(ASTPattern::TagStruct(), v.span(), v.path, std::move(subpats), true));
                }

                void visit(ASTExprNodeStructLiteralPattern& v) override {
                    std::vector<ASTStructPatternEntry> subpats;
                    for (auto& m : v.values) {
                        subpats.push_back(ASTStructPatternEntry{std::move(m.attrs), m.name, lower(m.value)});
                    }
                    pat(ASTPattern(ASTPattern::TagStruct(), v.span(), v.path, std::move(subpats), false));
                }

                void visit(ASTExprNodeArray& v) override {
                    if (v.size) {
                        TODO(v.span(), "Sized Array literal in destructured assignment");
                    } else {
                        bool isSplit = false;
                        std::vector<ASTPattern> leading;
                        std::vector<ASTPattern> trailing;
                        for (auto& m : v.values) {
                            if (isRest(m)) {
                                ASSERT_BUG(v.span(), !isSplit, "Multiple `..` in slice pattern?");
                                isSplit = true;
                                leading = std::move(trailing);
                                continue;
                            }
                            trailing.push_back(lower(m));
                        }
                        if (isSplit) {
                            pat(ASTPattern(v.span(), ASTPattern::Data::make_SplitSlice({std::move(leading), ASTPatternBinding(), std::move(trailing)})));
                        } else {
                            pat(ASTPattern(v.span(), ASTPattern::Data::make_Slice({std::move(trailing)})));
                        }
                    }
                }

                void visit(ASTExprNodeTuple& v) override {
                    pat(ASTPattern(ASTPattern::TagTuple(), v.span(), lowerTuplePat(v.span(), v.values)));
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
                    slot(v);
                }

                void visit(ASTExprNodeBinOp& v) override {
                    // `(..) = value` -- the parens are not a tuple, so the whole
                    // left side is the bare rest pattern.
                    if (v.type == ASTExprNodeBinOp::RANGE && !v.left && !v.right) {
                        pat(ASTPattern(ASTPattern::TagTuple(), v.span(), ASTPattern::TuplePat{{}, true, {}}));
                        return;
                    }
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
                rv->nodes.push_back({true, ASTExprNodeP(new ASTExprNodeLetBinding(std::move(pat), mkType(*parentExpandState.crate.pool, node.span()), std::move(node.value)))});
                for (auto& slots : v.slots) {
                    rv->nodes.push_back({true, ASTExprNodeP(new ASTExprNodeAssign(ASTExprNodeAssign::NONE, std::move(slots.second), ASTExprNodeP(new ASTExprNodeNamedValue(ASTPath::newLocal(std::move(slots.first))))))});
                }
                this->replacement = ASTExprNodeP(rv);
            }
        }
    }

    void visit(ASTExprNodeCallPath& node) override {
        ExpandPath(this->expandState, this->curMod(), node.path);
        this->visitVector(node.args);
    }

    void visit(ASTExprNodeCallMethod& node) override {
        ExpandPathParams(this->expandState, this->curMod(), node.method.args());
        this->visitNodelete(node, node.val);
        this->visitVector(node.args);
    }

    void visit(ASTExprNodeCallObject& node) override {
        this->visitNodelete(node, node.val);
        this->visitVector(node.args);
    }

    void visit(ASTExprNodeLoop& node) override {
        this->visitNodelete(node, node.code);
    }

    void visit(ASTExprNodeFor& node) override {
        ExpandPattern(this->expandState, this->curMod(), node.pattern, false);
        this->visitNodelete(node, node.value);
        this->visitNodelete(node, node.code);

        static const RcString rcstringIntoIter = RcString::newInterned("into_iter");
        static const RcString rcstringIntoAsyncIter = RcString::newInterned("into_async_iter");
        static const RcString rcstringNext = RcString::newInterned("next");
        static const RcString rcstringIt = RcString::newInterned("it");
        // Hygiene outlives the AST (idents in HIR patterns and macro_rules
        // keep pointing at it), so it lives in the persistent pool.
        const auto iteratorHygiene = Ident::Hygiene::newScope(*parentExpandState.crate.hirPool);
        auto coreCrate = crate.extCratenameCore;
        auto pathSome = getPath(coreCrate, "option", "Option", "Some");
        auto pathNone = getPath(coreCrate, "option", "Option", "None");
        auto pathIntoIterator = node.isAwait ? getPath(coreCrate, "async_iter", "IntoAsyncIterator") : getPath(coreCrate, "iter", "IntoIterator");
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
        arms.push_back(ASTExprNodeMatchArm(::makeVec1(ASTPattern(ASTPattern::TagNamedTuple(), node.span(), pathSome, ::makeVec1(mv$(node.pattern)))), {}, mv$(node.code)));
        // - `None => break label`
        arms.push_back(ASTExprNodeMatchArm(::makeVec1(ASTPattern(ASTPattern::TagValue(), node.span(), ASTPattern::Value::make_Named(pathNone))), {}, ASTExprNodeP(new ASTExprNodeFlow(ASTExprNodeFlow::BREAK, node.label, nullptr))));

        auto nextReceiver = ASTExprNodeP(new ASTExprNodeNamedValue(ASTPath::newRelative(iteratorHygiene, ::makeVec1(ASTPathNode(rcstringIt)))));
        // An async iterator is polled where it stands: the await takes the place
        // itself, and borrows it once per poll.
        auto nextCall = node.isAwait
            ? ASTExprNodeP(new ASTExprNodeUniOp(ASTExprNodeUniOp::AWaitNext, mv$(nextReceiver)))
            : ASTExprNodeP(new ASTExprNodeCallPath(ASTPath::newUfcsTrait(::mkType(*parentExpandState.crate.pool, node.span()), pathIterator, {ASTPathNode(rcstringNext)}), ::makeVec1(ASTExprNodeP(new ASTExprNodeUniOp(ASTExprNodeUniOp::REFMUT, mv$(nextReceiver))))));
        auto nextMatch = ASTExprNodeP(new ASTExprNodeMatch(mv$(nextCall), mv$(arms)));
        auto loop = ASTExprNodeP(new ASTExprNodeLoop(node.label, mv$(nextMatch)));

        auto intoIterCall = ASTExprNodeP(new ASTExprNodeCallPath(ASTPath::newUfcsTrait(::mkType(*parentExpandState.crate.pool, node.span()), pathIntoIterator, {ASTPathNode(node.isAwait ? rcstringIntoAsyncIter : rcstringIntoIter)}), ::makeVec1(mv$(node.value))));
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
        this->visitNodelete(node, node.code);
    }

    /// Lift each `pat if expr` out of a pattern, leaving the pattern it guards
    /// behind. The conditions become the arm's own guards, which is where they
    /// are evaluated: after every binding of the arm is in scope.
    static void liftGuardPatterns(ASTPattern& pat, ::std::vector<ASTIfLetCondition>& out) {
        switch (pat.data().tag()) {
default:
            break;
            case ASTPatternData::TAG_Guard: {
                auto& e = pat.data().as_Guard();
                auto sub = mv$(*e.sub);
                auto cond = mv$(e.cond);
                for (auto& b : pat.bindings()) {
                    sub.bindings().push_back(mv$(b));
                }
                pat = mv$(sub);
                liftGuardPatterns(pat, out);
                out.push_back(ASTIfLetCondition{nullptr, mv$(cond)});
                return;
            }
            case ASTPatternData::TAG_Box: {
                auto& e = pat.data().as_Box();
                liftGuardPatterns(*e.sub, out);
                break;
            }
            case ASTPatternData::TAG_Deref: {
                auto& e = pat.data().as_Deref();
                liftGuardPatterns(*e.sub, out);
                break;
            }
            case ASTPatternData::TAG_Ref: {
                auto& e = pat.data().as_Ref();
                liftGuardPatterns(*e.sub, out);
                break;
            }
            case ASTPatternData::TAG_Tuple: {
                auto& e = pat.data().as_Tuple();
                for (auto& sub : e.start) {
                    liftGuardPatterns(sub, out);
                }
                for (auto& sub : e.end) {
                    liftGuardPatterns(sub, out);
                }
                break;
            }
            case ASTPatternData::TAG_StructTuple: {
                auto& e = pat.data().as_StructTuple();
                for (auto& sub : e.tupPat.start) {
                    liftGuardPatterns(sub, out);
                }
                for (auto& sub : e.tupPat.end) {
                    liftGuardPatterns(sub, out);
                }
                break;
            }
            case ASTPatternData::TAG_Struct: {
                auto& e = pat.data().as_Struct();
                for (auto& sub : e.subPatterns) {
                    liftGuardPatterns(sub.pat, out);
                }
                break;
            }
            case ASTPatternData::TAG_Slice: {
                auto& e = pat.data().as_Slice();
                for (auto& sub : e.subPats) {
                    liftGuardPatterns(sub, out);
                }
                break;
            }
            case ASTPatternData::TAG_SplitSlice: {
                auto& e = pat.data().as_SplitSlice();
                for (auto& sub : e.leading) {
                    liftGuardPatterns(sub, out);
                }
                for (auto& sub : e.trailing) {
                    liftGuardPatterns(sub, out);
                }
                break;
            }
            case ASTPatternData::TAG_Or: {
                auto& e = pat.data().as_Or();
                for (auto& sub : e) {
                    liftGuardPatterns(sub, out);
                }
                break;
            }
        }
    }

    void visit(ASTExprNodeMatch& node) override {
        this->visitNodelete(node, node.val);
        for (auto& arm : node.arms) {
            ExpandAttrsCfgAttr(*expandState.wb.settings, arm.attrs);
            ExpandAttrs(expandState, arm.attrs, AttrStage::Pre, makeCallable<ExpandAttrCb>([&](const Span& sp, const auto& d, const auto& a) {
                d.handle(sp, a, expandState.wb, crate, arm);
            }));
            if (arm.patterns.size() == 0) {
                continue;
            }
            for (auto& pat : arm.patterns) {
                ExpandPattern(this->expandState, this->curMod(), pat, true);
            }
            {
                ::std::vector<ASTIfLetCondition> patternGuards;
                for (auto& pat : arm.patterns) {
                    liftGuardPatterns(pat, patternGuards);
                }
                for (auto& guard : patternGuards) {
                    arm.guard.push_back(mv$(guard));
                }
            }
            for (auto& cond : arm.guard) {
                if (cond.optPat) {
                    ExpandPattern(this->expandState, this->curMod(), *cond.optPat, true);
                }
                this->visitNodelete(node, cond.value);
            }

            this->visitNodelete(node, arm.code);
            ExpandAttrs(expandState, arm.attrs, AttrStage::Post, makeCallable<ExpandAttrCb>([&](const Span& sp, const auto& d, const auto& a) {
                d.handle(sp, a, expandState.wb, crate, arm);
            }));
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

    void visit(ASTExprNodeSuffixedLiteral& node) override {
    }

    void visit(ASTExprNodeClosure& node) override {
        auto tryStack = ::std::move(this->tryStack);
        for (auto& arg : node.args) {
            ExpandPattern(this->expandState, this->curMod(), arg.first, false);
            ExpandType(this->expandState, this->curMod(), arg.second);
        }
        ExpandType(this->expandState, this->curMod(), node.returnType);
        this->visitNodelete(node, node.code);
        this->tryStack = std::move(tryStack);
    }

    void visit(ASTExprNodeStructLiteral& node) override {
        this->visitNodelete(node, node.baseValue);
        for (auto& val : node.values) {
            ExpandAttrsCfgAttr(*expandState.wb.settings, val.attrs);
            ExpandAttrs(expandState, val.attrs, AttrStage::Pre, makeCallable<ExpandAttrCb>([&](const Span& sp, const auto& d, const auto& a) {
                d.handle(sp, a, expandState.wb, crate, val);
            }));
            if (!val.value) {
                continue;
            }
            this->visitNodelete(node, val.value);
            ExpandAttrs(expandState, val.attrs, AttrStage::Post, makeCallable<ExpandAttrCb>([&](const Span& sp, const auto& d, const auto& a) {
                d.handle(sp, a, expandState.wb, crate, val);
            }));
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
            ExpandAttrs(expandState, val.attrs, AttrStage::Pre, makeCallable<ExpandAttrCb>([&](const Span& sp, const auto& d, const auto& a) {
                d.handle(sp, a, expandState.wb, crate, val);
            }));
            if (!val.value) {
                continue;
            }
            this->visitNodelete(node, val.value);
            ExpandAttrs(expandState, val.attrs, AttrStage::Post, makeCallable<ExpandAttrCb>([&](const Span& sp, const auto& d, const auto& a) {
                d.handle(sp, a, expandState.wb, crate, val);
            }));
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
        this->visitNodelete(node, node.size);
        this->visitVector(node.values);
    }

    void visit(ASTExprNodeTuple& node) override {
        this->visitVector(node.values);
    }

    void visit(ASTExprNodeNamedValue& node) override {
        ExpandPath(this->expandState, this->curMod(), node.path);
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
        this->visitNodelete(node, node.value);
    }

    void visit(ASTExprNodeCast& node) override {
        this->visitNodelete(node, node.value);
        ExpandType(this->expandState, this->curMod(), node.type);
    }

    void visit(ASTExprNodeTypeAnnotation& node) override {
        this->visitNodelete(node, node.value);
        ExpandType(this->expandState, this->curMod(), node.type);
    }

    void visit(ASTExprNodeBinOp& node) override {
        this->visitNodelete(node, node.left);
        this->visitNodelete(node, node.right);

        if (this->inAssignLhs) {
            return;
        }
        static const RcString rcstringStart = RcString::newInterned("start");
        static const RcString rcstringEnd = RcString::newInterned("end");
        static const RcString rcstringNewRange = RcString::newInterned("new_range");
        const bool newRange = crate.features.count(rcstringNewRange) != 0;
        switch (node.type) {
            case ASTExprNodeBinOp::RANGE: {
                // NOTE: Not language items pre 1.39
                auto coreCrate = crate.extCratenameCore;
                auto pathRange = getPath(coreCrate, newRange ? "range" : "ops", "Range");
                auto pathRangeFrom = getPath(coreCrate, newRange ? "range" : "ops", "RangeFrom");
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
                auto pathRangeInclusiveNonEmpty = getPath(coreCrate, newRange ? "range" : "ops", "RangeInclusive");
                auto pathRangeToInclusive = getPath(coreCrate, "ops", "RangeToInclusive");

                if (node.left) {
                    ASTExprNodeStructLiteral::tValues values;
                    values.push_back({{}, rcstringStart, mv$(node.left)});
                    values.push_back({{}, rcstringEnd, mv$(node.right)});
                    if (!newRange) {
                        values.push_back({{}, RcString::newInterned("exhausted"), ASTExprNodeP(new ASTExprNodeBool(false))});
                    }
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
        this->visitNodelete(node, node.value);
        // `&pin mut place` pins the place: `Pin::new_unchecked` of a borrow of it.
        // The borrow is what keeps the place from moving, so the call is sound
        // wherever the borrow is.
        if (node.type == ASTExprNodeUniOp::PinBorrow || node.type == ASTExprNodeUniOp::PinBorrowMut) {
            const bool isMut = node.type == ASTExprNodeUniOp::PinBorrowMut;
            auto pathNewUnchecked = getPath(crate.extCratenameCore, "pin", "Pin", "new_unchecked");
            auto borrow = ASTExprNodeP(new ASTExprNodeUniOp(isMut ? ASTExprNodeUniOp::REFMUT : ASTExprNodeUniOp::REF, mv$(node.value)));
            borrow->setSpan(node.span());
            auto call = ASTExprNodeP(new ASTExprNodeCallPath(mv$(pathNewUnchecked), ::makeVec1(mv$(borrow))));
            call->setSpan(node.span());
            auto block = new ASTExprNodeBlock();
            block->blockType = ASTExprNodeBlock::Type::Unsafe;
            block->nodes.push_back({false, mv$(call)});
            replacement.reset(block);
            replacement->setSpan(node.span());
            return;
        }
        // - Desugar question mark operator before resolve so it can create names
        if (node.type == ASTExprNodeUniOp::QMARK) {
            if (expandState.mode != ExpandMode::Final) {
                return;
            }

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
                        (tryStack.empty() ? ASTExprNodeFlow::RETURN : ASTExprNodeFlow::BREAK), // NOTE: uses `break 'tryblock` instead of return if in a try block.
                        (tryStack.empty() ? RcString("") : tryStack.back()),
                        ASTExprNodeP(new ASTExprNodeCallPath(ASTPath(pathFromResidualFromResidual), ::makeVec1(ASTExprNodeP(new ASTExprNodeNamedValue(ASTPath(rcstringR))))))
                    ))
                ));

                replacement.reset(new ASTExprNodeMatch(ASTExprNodeP(new ASTExprNodeCallPath(mv$(pathTryBranch), ::makeVec1(mv$(node.value)))), mv$(arms)));
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
    for (auto& paramDef : params.params) {
        switch (paramDef.tag()) {
            case GenericParam::TAG_None: {
                // Ignore
                break;
            }
            case GenericParam::TAG_Lifetime: {
                break;
            }
            case GenericParam::TAG_Type: {
                auto& tyDef = paramDef.as_Type();
                ExpandType(es, mod, tyDef.getDefault());
                break;
            }
            case GenericParam::TAG_Value: {
                auto& valDef = paramDef.as_Value();
                ExpandType(es, mod, valDef.type());
                break;
            }
        }
    }
    for (auto& bound : params.bounds) {
        switch (bound.tag()) {
            case ASTGenericBound::TAG_None: {
                break;
            }
            case ASTGenericBound::TAG_Lifetime: {
                break;
            }
            case ASTGenericBound::TAG_TypeLifetime: {
                auto& be = bound.as_TypeLifetime();
                ExpandType(es, mod, be.type);
                break;
            }
            case ASTGenericBound::TAG_IsTrait: {
                auto& be = bound.as_IsTrait();
                ExpandType(es, mod, be.type); ExpandPath(es, mod, be.trait);
                break;
            }
            case ASTGenericBound::TAG_MaybeTrait: {
                auto& be = bound.as_MaybeTrait();
                ExpandType(es, mod, be.type); ExpandPath(es, mod, be.trait);
                break;
            }
            case ASTGenericBound::TAG_NotTrait: {
                auto& be = bound.as_NotTrait();
                ExpandType(es, mod, be.type); ExpandPath(es, mod, be.trait);
                break;
            }
            case ASTGenericBound::TAG_Equality: {
                auto& be = bound.as_Equality();
                ExpandType(es, mod, be.type); ExpandType(es, mod, be.replacement);
                break;
            }
        }
    }
    for (auto& t : params.bareBoundTypes) {
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
    if (auto* delegation = e.delegation()) {
        for (auto& target : delegation->targets) {
            ExpandPath(es, mod, target.path);
        }
        ExpandExpr(es, delegation->body);
    }
    for (size_t i = 0; i < e.args().size(); i++) {
        auto& arg = e.args()[i];
        if (!ExpandAttrsCfgOnly(es, arg.attrs)) {
            e.args().erase(e.args().begin() + i);
            i--;
            continue;
        }
        ExpandPattern(es, mod, arg.pat, false);
        ExpandType(es, mod, arg.ty);
        ExpandAttrs(es, arg.attrs, AttrStage::Post, makeCallable<ExpandAttrCb>([&](const Span& sp, const ExpandDecorator& d, const ASTAttribute& a) {
            TODO(sp, "attributes on function arguments - " << a);
        }));
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

        switch ((*i.data).tag()) {
default:
            BUG(Span(), "Unknown item type in impl block - " << i.data->tagStr());
            case ASTItem::TAG_None: {
                break;
            }
            case ASTItem::TAG_MacroInv: {
                auto& e = (*i.data).as_MacroInv();
                if (e.path().isValid()) {
                    TRACE_FUNCTION_F("Macro invoke " << e.path());
                    // Move out of the module to avoid invalidation if a new macro invocation is added
                    auto miOwned = mv$(e);

                    // A macro that is not in scope yet (one a `macro_rules!`
                    // later in the enclosing block defines) is left where it is
                    // and retried on the next pass.
                    auto ttl = ExpandMacro(es, mod, miOwned);

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
                break;
            }
            case ASTItem::TAG_Function: {
                auto& e = (*i.data).as_Function();
                TRACE_FUNCTION_F("fn " << i.name);
                ExpandFunction(es, mod, e);
                break;
            }
            case ASTItem::TAG_Static: {
                auto& e = (*i.data).as_Static();
                TRACE_FUNCTION_F("static " << i.name);
                ExpandGenericParams(es, mod, e.params());
                ExpandExpr(es, e.value());
                ExpandType(es, mod, e.type());
                break;
            }
            case ASTItem::TAG_Type: {
                auto& e = (*i.data).as_Type();
                TRACE_FUNCTION_F("type " << i.name);
                ExpandType(es, mod, e.type());
                break;
            }
        }
        impl.items()[idx] = std::move(i);

        // Run post-expansion decorators and restore attributes
        {
            auto& i = impl.items()[idx];
            ExpandAttrs(es, attrs, AttrStage::Post, mod, impl, i.vis, i.name, *i.data);
            // TODO: How would this be populated? It got moved out?
            if (i.attrs.items.size() == 0) {
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
        switch (dat.tag()) {
default:
            BUG(Span(), "Unexpected item type - " << dat.tagStr());
            case ASTItem::TAG_None: {
                // Skip: nothing
                break;
            }
            case ASTItem::TAG_Type: {
                auto& e = dat.as_Type();
                ExpandType(es, mod, e.type());
                break;
            }
            case ASTItem::TAG_Function: {
                auto& e = dat.as_Function();
                ExpandFunction(es, mod, e);
                break;
            }
            case ASTItem::TAG_Static: {
                auto& e = dat.as_Static();
                ExpandGenericParams(es, mod, e.params());
                ExpandExpr(es, e.value());
                ExpandType(es, mod, e.type());
                break;
            }
            case ASTItem::TAG_MacroInv: {
                auto& e = dat.as_MacroInv();
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
                break;
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
                mod.addItem(Span(), ASTVisibility::makeRestricted(ASTVisibility::Ty::Private, mod.path()), "", ASTUseItem{Span(), true, ::makeVec1(ASTUseItem::Ent{Span(), es.crate.preludePath, ""})}, {});
            } else {
                DEBUG("> Not inserting custom prelude (anon or disabled)");
            }
        }
    }

    // Stack to prevent macro recursion
    // - Items are popped if the item address matches
    std::vector<const ASTNamed<ASTItem>*> macroRecursionStack;

    DEBUG("Items");
    for (unsigned int idx = firstItem; idx < mod.items.size(); idx++) {
        auto& i = *mod.items[idx];

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
                        return !checkCfgAttrs(settings, v.attrs);
                    });
                    DEBUG(lst.size() << " -> " << newEnd - lst.begin());
                    lst.erase(newEnd, lst.end());
                }

                static void filterCfg(const Settings& settings, ::std::vector<ASTTupleItem>& lst) {
                    auto newEnd = ::std::remove_if(lst.begin(), lst.end(), [&](const ASTTupleItem& v) {
                        return !checkCfgAttrs(settings, v.attrs);
                    });
                    DEBUG(lst.size() << " -> " << newEnd - lst.begin());
                    lst.erase(newEnd, lst.end());
                }
            };

            DEBUG(i.data.tagStr() << " " << mod.path() << "::" << i.name);
            switch (i.data.tag()) {
                case ASTItem::TAG_Struct: {
                    auto& str = i.data.as_Struct();
                    switch (str.data.tag()) {
                        case ASTStructData::TAG_Unit: {
                            break;
                        }
                        case ASTStructData::TAG_Struct: {
                            auto& e = str.data.as_Struct();
                            H::filterCfg(*es.wb.settings, e.ents);
                            break;
                        }
                        case ASTStructData::TAG_Tuple: {
                            auto& e = str.data.as_Tuple();
                            H::filterCfg(*es.wb.settings, e.ents);
                            break;
                        }
                    }
                    break;
                }
                case ASTItem::TAG_Union: {
                    auto& unm = i.data.as_Union();
                    H::filterCfg(*es.wb.settings, unm.variants);
                    break;
                }
                case ASTItem::TAG_Enum: {
                    auto& enm = i.data.as_Enum();
                    for (auto it = enm.variants().begin(); it != enm.variants().end();) {
                        if (!checkCfgAttrs(*es.wb.settings, it->attrs)) {
                            it = enm.variants().erase(it);
                        } else {
                        switch (it->data.tag()) {
                            case ASTEnumVariantData::TAG_Unit: {
                                break;
                            }
                            case ASTEnumVariantData::TAG_Tuple: {
                                auto& e = it->data.as_Tuple();
                                H::filterCfg(*es.wb.settings, e.items);
                                break;
                            }
                            case ASTEnumVariantData::TAG_Struct: {
                                auto& e = it->data.as_Struct();
                                H::filterCfg(*es.wb.settings, e.fields);
                                break;
                            }
                        }

                        ++ it;
                        }
                    }
                    break;
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

        switch (dat.tag()) {
            case ASTItem::TAG_None: {
                // Skip: nothing
                break;
            }
            case ASTItem::TAG_GlobalAsm: {
                auto& e = dat.as_GlobalAsm();
                for (auto& operand : e.operands) {
                    switch (operand.tag()) {
                        case ASTGlobalAsmOperand::TAG_Const: {
                            auto& expr = operand.as_Const();
                            ExpandExpr(es, expr);
                            break;
                        }
                        case ASTGlobalAsmOperand::TAG_Sym: {
                            auto& sym = operand.as_Sym();
                            ExpandPath(es, mod, sym);
                            break;
                        }
                    }
                }
                break;
            }
            case ASTItem::TAG_MacroInv: {
                auto& e = dat.as_MacroInv();
                // Move out of the module to avoid invalidation if a new macro invocation is added

                if (macroRecursionStack.size() > es.wb.settings->recursionLimit) {
                    ERROR(i.span, E0000, "Exceeded macro recusion limit of " << es.wb.settings->recursionLimit);
                }
                auto miOwned = mv$(e);

                if (!miOwned.isExpanded()) {
                    assert(miOwned.span());
                    TRACE_FUNCTION_F("Macro invoke " << miOwned.path());

                    auto ttl = ExpandMacro(es, mod, miOwned);
                    if (ttl) {
                        // Parse
                        DEBUG("-- Parsing as mod items");
                        size_t oldLen = mod.items.size();
                        ParseModRootItemsInto(mod, idx, *ttl);

                        auto nextNonMacroItem = idx + 1 + (mod.items.size() - oldLen);
                        macroRecursionStack.push_back(nextNonMacroItem == mod.items.size() ? nullptr : &*mod.items[nextNonMacroItem]);

                        miOwned.setExpanded();
                    } else {
                        DEBUG("Deferred macro");
                    }
                }
                dat.as_MacroInv() = mv$(miOwned);
                break;
            }
            case ASTItem::TAG_Macro: {
                auto& e = dat.as_Macro();
                ASSERT_BUG(i.span, e, "Null macro - " << i.name);
                mod.addMacro(i.vis.isGlobal(), i.name, mv$(e));
                dat = ASTItem::make_None({});
                break;
            }
            case ASTItem::TAG_Use: {
                auto& e = dat.as_Use();
                // Determine if the `use` refers to a macro, and import into the current scope
                for (const auto& ue : e.entries) {
                    // Get module ref, if it's to a HIR module then grab the macro
                    if (ue.name != "" && ue.path.nodes().size() >= 1) {
                        DEBUG("Use " << ue.path);

                        ASTAbsolutePath refPath;
                        auto m = ResolveLookupMacro(ue.sp, *es.wb.settings, es.crate, mod.path(), ue.path, /*out_path=*/&refPath);
                        MacroRef ref;
                    switch (m.tag()) {
                        case ResolveItemRefMacro::TAG_None: {
                            // Not found? Ignore.
                            break;
                        }
                        case ResolveItemRefMacro::TAG_InternalMacro: {
                            // Ignore builtins, they're always available.
                            break;
                        }
                        case ResolveItemRefMacro::TAG_ProcMacro: {
                            auto& pm = m.as_ProcMacro();
                            ref = pm;
                            break;
                        }
                        case ResolveItemRefMacro::TAG_MacroRules: {
                            auto& mr = m.as_MacroRules();
                            ref = mr;
                            break;
                        }
                    }
                    if( ! ref.is_None() ) {
                            DEBUG(mod.path() << " + Macro Import: " << refPath);
                            mod.macroImports.push_back(ASTModule::MacroImport{false, ue.name, std::move(refPath), std::move(ref)});
                    }
                    }
                }
                break;
            }
            case ASTItem::TAG_ExternBlock: {
                auto& e = dat.as_ExternBlock();
                Expand_ExternBlock(es, mod, e);
                // HACK: Just convert inner items into outer items
                auto items = mv$(e.items());
                for (auto& i2 : items) {
                    mod.items.push_back(box$(i2));
                }
                break;
            }
            case ASTItem::TAG_Impl: {
                auto& e = dat.as_Impl();
                Expand_Impl(es, modpath, mod, e);
                break;
            }
            case ASTItem::TAG_NegImpl: {
                auto& e = dat.as_NegImpl();
                Expand_ImplDef(es, modpath, mod, e);
                break;
            }
            case ASTItem::TAG_Module: {
                throw "";
            }
            case ASTItem::TAG_Crate: {
                auto& e = dat.as_Crate();
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
                break;
            }
            case ASTItem::TAG_Struct: {
                auto& e = dat.as_Struct();
                ExpandGenericParams(es, mod, e.params());
                switch (e.data.tag()) {
                    case ASTStructData::TAG_Unit: {
                        break;
                    }
                    case ASTStructData::TAG_Struct: {
                        auto& sd = e.data.as_Struct();
                        for (auto it = sd.ents.begin(); it != sd.ents.end();) {
                            auto& si = *it;
                            ExpandAttrsCfgAttr(*es.wb.settings, si.attrs);
                            ExpandAttrs(es, si.attrs, AttrStage::Pre, makeCallable<ExpandAttrCb>([&](const Span& sp, const auto& d, const auto& a) {
                                d.handle(sp, a, es.wb, es.crate, si);
                            }));
                            ExpandType(es, mod, si.type);
                            ExpandExpr(es, si.defaultValue);
                            ExpandAttrs(es, si.attrs, AttrStage::Post, makeCallable<ExpandAttrCb>([&](const Span& sp, const auto& d, const auto& a) {
                                d.handle(sp, a, es.wb, es.crate, si);
                            }));

                            if (si.name == "") {
                                it = sd.ents.erase(it);
                            } else {
                                ++it;
                            }
                        }
                        break;
                    }
                    case ASTStructData::TAG_Tuple: {
                        auto& sd = e.data.as_Tuple();
                        for (auto it = sd.ents.begin(); it != sd.ents.end();) {
                            auto& si = *it;
                            ExpandAttrsCfgAttr(*es.wb.settings, si.attrs);
                            ExpandAttrs(es, si.attrs, AttrStage::Pre, makeCallable<ExpandAttrCb>([&](const Span& sp, const auto& d, const auto& a) {
                                d.handle(sp, a, es.wb, es.crate, si);
                            }));
                            ExpandType(es, mod, si.type);
                            ExpandAttrs(es, si.attrs, AttrStage::Post, makeCallable<ExpandAttrCb>([&](const Span& sp, const auto& d, const auto& a) {
                                d.handle(sp, a, es.wb, es.crate, si);
                            }));

                            if (!si.type->isValid()) {
                                it = sd.ents.erase(it);
                            } else {
                                ++it;
                            }
                        }
                        break;
                    }
                }
                break;
            }
            case ASTItem::TAG_Enum: {
                auto& e = dat.as_Enum();
                ExpandGenericParams(es, mod, e.params());
                for (auto& var : e.variants()) {
                    ExpandAttrsCfgAttr(*es.wb.settings, var.attrs);
                    ExpandAttrs(es, var.attrs, AttrStage::Pre, makeCallable<ExpandAttrCb>([&](const Span& sp, const auto& d, const auto& a) {
                        d.handle(sp, a, es.wb, es.crate, var);
                    }));
                switch (var.data.tag()) {
                    case ASTEnumVariantData::TAG_Unit: {
                        break;
                    }
                    case ASTEnumVariantData::TAG_Tuple: {
                        auto& e = var.data.as_Tuple();
                        for (auto it = e.items.begin(); it != e.items.end();) {
                            auto& si = *it;
                            ExpandAttrsCfgAttr(*es.wb.settings, si.attrs);
                            ExpandAttrs(es, si.attrs, AttrStage::Pre, makeCallable<ExpandAttrCb>([&](const Span& sp, const auto& d, const auto& a) {
                                d.handle(sp, a, es.wb, es.crate, si);
                            }));
                            ExpandType(es, mod, si.type);
                            ExpandAttrs(es, si.attrs, AttrStage::Pre, makeCallable<ExpandAttrCb>([&](const Span& sp, const auto& d, const auto& a) {
                                d.handle(sp, a, es.wb, es.crate, si);
                            }));
                            if (!si.type->isValid()) {
                                it = e.items.erase(it);
                            } else {
                                ++it;
                            }
                        }
                        break;
                    }
                    case ASTEnumVariantData::TAG_Struct: {
                        auto& e = var.data.as_Struct();
                        for (auto it = e.fields.begin(); it != e.fields.end();) {
                            auto& si = *it;
                            ExpandAttrsCfgAttr(*es.wb.settings, si.attrs);
                            ExpandAttrs(es, si.attrs, AttrStage::Pre, makeCallable<ExpandAttrCb>([&](const Span& sp, const auto& d, const auto& a) {
                                d.handle(sp, a, es.wb, es.crate, si);
                            }));
                            ExpandType(es, mod, si.type);
                            ExpandExpr(es, si.defaultValue);
                            ExpandAttrs(es, si.attrs, AttrStage::Post, makeCallable<ExpandAttrCb>([&](const Span& sp, const auto& d, const auto& a) {
                                d.handle(sp, a, es.wb, es.crate, si);
                            }));

                            if (si.name == "") {
                                it = e.fields.erase(it);
                            } else {
                                ++it;
                            }
                        }
                        break;
                    }
                }
                ExpandExpr(es,  var.discriminantValue);
                ExpandAttrs(es, var.attrs, AttrStage::Post,  makeCallable<ExpandAttrCb>([&](const Span& sp, const auto& d, const auto& a){
                        d.handle(sp, a, es.wb, es.crate, var); }));
                }
                // Handle cfg on variants (kinda hacky)
                for (auto it = e.variants().begin(); it != e.variants().end();) {
                    if (it->name == "") {
                        it = e.variants().erase(it);
                    } else {
                        ++it;
                    }
                }
                break;
            }
            case ASTItem::TAG_Union: {
                auto& e = dat.as_Union();
                ExpandGenericParams(es, mod, e.params_);
                for (auto it = e.variants.begin(); it != e.variants.end();) {
                    auto& si = *it;
                    ExpandAttrsCfgAttr(*es.wb.settings, si.attrs);
                    ExpandAttrs(es, si.attrs, AttrStage::Pre, makeCallable<ExpandAttrCb>([&](const Span& sp, const auto& d, const auto& a) {
                        d.handle(sp, a, es.wb, es.crate, si);
                    }));
                    ExpandType(es, mod, si.type);
                    ExpandExpr(es, si.defaultValue);
                    ExpandAttrs(es, si.attrs, AttrStage::Post, makeCallable<ExpandAttrCb>([&](const Span& sp, const auto& d, const auto& a) {
                        d.handle(sp, a, es.wb, es.crate, si);
                    }));

                    if (si.name == "") {
                        it = e.variants.erase(it);
                    } else {
                        ++it;
                    }
                }
                break;
            }
            case ASTItem::TAG_Trait: {
                auto& e = dat.as_Trait();
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

                switch (ti.data.tag()) {
default:
                    BUG(Span(), "Unknown item type in trait block - " << ti.data.tagStr());
                    case ASTItem::TAG_None: {
                        break;
                    }
                    case ASTItem::TAG_MacroInv: {
                        auto& e = ti.data.as_MacroInv();
                        if (e.path().isValid()) {
                            TRACE_FUNCTION_F("Macro invoke " << e.path());
                            // Move out of the module to avoid invalidation if a new macro invocation is added
                            auto miOwned = mv$(e);

                            // As in an impl block: a macro that is not in
                            // scope yet is retried on the next pass.
                            auto ttl = ExpandMacro(es, mod, miOwned);

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
                        break;
                    }
                    case ASTItem::TAG_Function: {
                        auto& e = ti.data.as_Function();
                        ExpandFunction(es, mod, e);
                        break;
                    }
                    case ASTItem::TAG_Static: {
                        auto& e = ti.data.as_Static();
                        ExpandGenericParams(es, mod, e.params());
                        ExpandExpr(es, e.value());
                        ExpandType(es, mod, e.type());
                        break;
                    }
                    case ASTItem::TAG_Type: {
                        auto& e = ti.data.as_Type();
                        ExpandType(es, mod, e.type());
                        break;
                    }
                }

                {
                        auto& ti = traitItems[idx];

                        ExpandAttrs(es, attrs, AttrStage::Post, tiPath, mod, e, ti.data);
                        if (ti.attrs.items.size() == 0) {
                            ti.attrs = mv$(attrs);
                        }
                }
                }
                break;
            }
            case ASTItem::TAG_Type: {
                auto& e = dat.as_Type();
                ExpandType(es, mod, e.type());
                break;
            }
            case ASTItem::TAG_Function: {
                auto& e = dat.as_Function();
                ExpandFunction(es, mod, e);
                break;
            }
            case ASTItem::TAG_Static: {
                auto& e = dat.as_Static();
                ExpandGenericParams(es, mod, e.params());
                ExpandExpr(es, e.value());
                ExpandType(es, mod, e.type());
                break;
            }
            case ASTItem::TAG_TraitAlias: {
                auto& e = dat.as_TraitAlias();
                for (auto& p : e.traits) {
                    ExpandPath(es, mod, *p.ent.path);
                }
                break;
            }
        }
        ExpandAttrs(es, attrs, AttrStage::Post,  path, mod, idx, vis, dat);

        {
            auto& i = *mod.items[idx];
            if (i.data.isDead()) {
                i.data = mv$(dat);
            }
            // TODO: When would this _not_ be empty?
            if (i.attrs.items.size() == 0) {
                i.attrs = mv$(attrs);
            }
        }
    }

    // IGNORE m_anon_modules, handled as part of expressions
}

void ExpandModIndexAnon(ASTCrate& crate, ASTModule& mod) {
    TRACE_FUNCTION_F("mod=" << mod.path());

    for (auto& i : mod.items) {
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
    for (auto& i : mod.items) {
        if (const auto* mi = i->data.opt_MacroInv()) {
            if (mi->path().isTrivial() && mi->path().asTrivial() == "macro_rules") {
                i->vis = ASTVisibility::makeGlobal();
                DEBUG("macro_rules made pub");
            }
        }

        ExpandAttrsCfgAttr(*wb.settings, i->attrs);
        bool isMacroExport = false;
        bool cfgFailed = false;
        for (auto& a : i->attrs.items) {
            if (a.name() == "cfg") {
                if (!checkCfg(*wb.settings, i->span, a)) {
                    cfgFailed = true;
                }
            } else if (a.name() == "macro_export") {
                isMacroExport = true;
            } else if (a.name() == "rustc_macro_transparency") {
                // Only `transparent` differs from what a macro does anyway: the
                // expansion's own names behave as if written at the call site.
                if (i->data.is_Macro() && a.parseEqualsString(wb, crate, mod) == "transparent") {
                    i->data.as_Macro()->transparent = true;
                }
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
    for (unsigned int idx = 0; idx < mod.items.size(); idx++) {
        auto& i = *mod.items[idx];
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
                // `macro_rules! {}` names no macro, so it is a call to a
                // user-defined macro that happens to be called `macro_rules`,
                // not a definition. It needs the ordinary pass, which searches
                // the enclosing modules before the builtin.
                if (mi->inputIdent() == "") {
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
                mod.items[idx]->data.as_MacroInv() = mv$(miOwned);
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

    ExpandState es{wb, crate, LList<const ASTModule*>(nullptr, &crate.rootModule_), ExpandMode::FirstPass};

    // 1. Crate attributes
    ExpandAttrsCfgAttr(*es.wb.settings, crate.attrs);
    ExpandAttrs(es, crate.attrs, AttrStage::Pre, makeCallable<ExpandAttrCb>([&](const Span& sp, const auto& d, const auto& a) {
        d.handle(sp, a, es.wb, crate);
    }));

    // TODO: Crate name and type

    std::vector<std::unique_ptr<ASTNamed<ASTItem>>> newRootItems;
    Expand_Mod_Early(wb, crate, crate.rootModule_, newRootItems);
    crate.rootModule_.items.insert(crate.rootModule_.items.begin(), std::make_move_iterator(newRootItems.begin()), std::make_move_iterator(newRootItems.end()));

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
        crate.rootModule_.items.push_back(box$(ASTNamed<ASTItem>(Span(), mv$(attrs), ASTVisibility::makeRestricted(ASTVisibility::Ty::Private, ASTAbsolutePath()), stdCrateShortname, ASTItem::make_Crate({stdCrateName}))));
        auto& i = *crate.rootModule_.items.back();
        ExpandAttrs(es, i.attrs, AttrStage::Post, ASTAbsolutePath(), crate.rootModule_, 0, i.vis, i.data);
    }

    // 2. Module attributes
    for (auto& a : crate.attrs.items) {
        for (auto& d : gDecorators) {
            if (a.name() == d.first && d.second->stage() == AttrStage::Pre) {
            }
        }
    }

    // 3. Module tree
    // Loop until no more expansions happen
    // - Combine this with allowing macros to fail to expand, to be caught with a final pass
    ExpandMod(es, ASTAbsolutePath(), crate.rootModule_);
    DEBUG("(first) es.change = " << es.change << ", es.has_missing=" << es.hasMissing << " (" << &es << ")");
    if (es.hasMissing) {
        for (size_t nIters = 0; nIters < 5 && es.change && es.hasMissing; nIters++) {
            es.mode = ExpandMode::Iterate;
            es.change = false;
            es.hasMissing = false;
            ExpandMod(es, ASTAbsolutePath(), crate.rootModule_);
            DEBUG("?(Iter) es.change = " << es.change << ", es.has_missing=" << es.hasMissing);
        }
        es.hasMissing = false;
    }
    es.mode = ExpandMode::Final;
    ExpandMod(es, ASTAbsolutePath(), crate.rootModule_);
    ASSERT_BUG(Span(), !es.hasMissing, "Expand too too many attempts");

    // Post-process
    ExpandModIndexAnon(crate, crate.rootModule_);

    // Extract exported macros

    {
        auto& exportedMacros = crate.exportedMacros;

        ::std::vector<ASTModule*> mods;
        mods.push_back(&crate.rootModule_);
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

            for (auto& i : mod.items) {
                if (i->data.is_Module()) {
                    mods.push_back(&i->data.as_Module());
                }
            }
        } while (mods.size() > 0);

        // - Exported macros imported by the root (is this needed?)
        // - Re-exported macros (ignore proc macros for now?)
        for (const auto& mac : crate.rootModule_.macroImports) {
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
