#include "expand_common.h"

#include "synext.h"
#include "ast_ast.h"
#include "hir_hir.h"
#include "ast_expr.h"
#include "settings.h"
#include "ast_crate.h"
#include "expand_cfg.h"
#include "wire_board.h"
#include "parse_common.h"
#include "main_bindings.h"
#include "parse_ttstream.h"
#include "resolve_common.h"
#include "expand_proc_macro.h"
#include "macro_rules_macro_rules.h"

void RegisterSynextBuiltins(ExpandRegistry& registry);
void RegisterCfgBuiltins(ExpandRegistry& registry);
void RegisterProcMacroBuiltins(ExpandRegistry& registry);

namespace {
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
        ASTModule* currentMod;
        mutable bool change;
        mutable bool hasMissing;

        ExpandState(const WireBoard& wb, ASTCrate& crate, LList<const ASTModule*> modstack, ExpandMode mode, ASTModule* currentMod);

        explicit ExpandState(const ExpandState&) = default;
    };

    struct ExpandAttrCallback {
        virtual void run(const Span& sp, const ExpandDecorator& d, const ASTAttribute& a) const = 0;
    };

    template <typename F>
    struct ExpandAttrCb final: ExpandAttrCallback {
        F f;

        explicit ExpandAttrCb(F f);

        void run(const Span& sp, const ExpandDecorator& d, const ASTAttribute& a) const override;
    };

    void ExpandAttrs(const ExpandState& es, const ASTAttributeList& attrs, AttrStage stage, const ExpandAttrCallback& f);
    void ExpandModExternCrates(const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& modpath, ASTModule& mod, unsigned int firstItem);
    void ExpandMod(const ExpandState& es, ASTAbsolutePath modpath, ASTModule& mod, unsigned int firstItem = 0);
    void ExpandExpr(const ExpandState& es, ASTExprNodeP& node);
    void ExpandExpr(const ExpandState& es, ASTExpr& node);
    void ExpandExpr(const ExpandState& es, ASTExprNode*& node);
    void ExpandPath(const ExpandState& es, ASTModule& mod, ASTPath& p);
    void ExpandPathParams(const ExpandState& es, ASTModule& mod, ASTPathParams& params);

    struct CExpandExpr: public ASTNodeVisitor {
        ASTCrate& crate;
        const ExpandState& parentExpandState;
        ExpandState expandState;
        ASTExprNodeP replacement;

        std::vector<RcString> tryStack;
        unsigned tryIndex = 0;

        ASTExprNodeBlock* currentBlock = nullptr;
        bool inAssignLhs = false;

        CExpandExpr(const ExpandState& es);

        ~CExpandExpr();

        template <typename T, typename... Args>
        ASTExprNodeP makeNode(Args&&... args) {
            return makeAstExprNode<T>(*crate.pool, std::forward<Args>(args)...);
        }

        ASTModule& curMod();

        void visit(ASTExprNodeP& cnode);

        void visitNodelete(const ASTExprNode& parent, ASTExprNodeP& cnode);

        void visitVector(std::vector<ASTExprNodeP>& cnodes);

        ASTExprNodeP visitMacro(ASTExprNodeMacro& node, std::vector<ASTExprNodeBlock::Line>* nodesOut);

        void visit(ASTExprNodeMacro& node) override;

        void visit(ASTExprNodeBlock& node) override;

        void visit(ASTExprNodeAsyncBlock& node) override;

        void visit(ASTExprNodeGeneratorBlock& node) override;

        void visit(ASTExprNodeTry& node) override;

        void visit(ASTExprNodeAsm& node) override;

        void visit(ASTExprNodeAsm2& node) override;

        void visit(ASTExprNodeFlow& node) override;

        void visit(ASTExprNodeLetBinding& node) override;

        void visit(ASTExprNodeAssign& node) override;

        void visit(ASTExprNodeCallPath& node) override;

        void visit(ASTExprNodeCallMethod& node) override;

        void visit(ASTExprNodeCallObject& node) override;

        void visit(ASTExprNodeLoop& node) override;

        void visit(ASTExprNodeFor& node) override;

        void visit(ASTExprNodeWhile& node) override;

        static void liftGuardPatterns(ASTPattern& pat, std::vector<ASTIfLetCondition>& out);

        void visit(ASTExprNodeMatch& node) override;

        void visit(ASTExprNodeIf& node) override;

        void visit(ASTExprNodeWildcardPattern& node) override;

        void visit(ASTExprNodeInteger& node) override;

        void visit(ASTExprNodeFloat& node) override;

        void visit(ASTExprNodeBool& node) override;

        void visit(ASTExprNodeString& node) override;

        void visit(ASTExprNodeByteString& node) override;

        void visit(ASTExprNodeCString& node) override;

        void visit(ASTExprNodeSuffixedLiteral& node) override;

        void visit(ASTExprNodeClosure& node) override;

        void visit(ASTExprNodeStructLiteral& node) override;

        void visit(ASTExprNodeStructLiteralPattern& node) override;

        void visit(ASTExprNodeArray& node) override;

        void visit(ASTExprNodeTuple& node) override;

        void visit(ASTExprNodeNamedValue& node) override;

        void visit(ASTExprNodeField& node) override;

        void visit(ASTExprNodeIndex& node) override;

        void visit(ASTExprNodeDeref& node) override;

        void visit(ASTExprNodeCast& node) override;

        void visit(ASTExprNodeTypeAnnotation& node) override;

        void visit(ASTExprNodeBinOp& node) override;

        void visit(ASTExprNodeUniOp& node) override;

        void visit(ASTExprNodeMacroDefinition&) override;
    };

    void ParseModRootItemsInto(ASTModule& mod, size_t idx, TokenStream& lex) {
        auto oldItems = std::move(mod.items);
        ParseModRootItems(lex, mod);
        oldItems.insert(oldItems.begin() + idx + 1, std::make_move_iterator(mod.items.begin()), std::make_move_iterator(mod.items.end()));
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
            a.markInert();
        };
        const RcString* builtinName = nullptr;
        if (a.name().elems.size() == 1) {
            builtinName = &a.name().elems[0];
        } else if (a.name().elems.size() == 4 && a.name().elems[0] == "core" && a.name().elems[1] == "prelude" && a.name().elems[2] == "v1") {
            // HACK: Handle `::core::prelude::v1::<FOO>`.
            builtinName = &a.name().elems[3];
        }
        if (builtinName) {
            if (const auto* d = ExpandFindDecorator(es.wb, *builtinName)) {
                found = true;
                if (d->stage() == stage && (d->runDuringIter() || (es.mode != ExpandMode::Final && stage == AttrStage::Pre) || (es.mode == ExpandMode::Final && stage == AttrStage::Post))) {
                    DEBUG("#[" << *builtinName << "]");
                    f.run(sp, *d, a);
                    a.markInert();
                }
            }
        }
        if (!found) {
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
                                if (const auto* d = ExpandFindDecorator(es.wb, target)) {
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
            } else if (const auto* procMacP = m.opt_ExternalProcMacro()) {
                const auto* procMac = *procMacP;

                struct ProcMacroDecorator: public ExpandDecorator {
                    std::vector<RcString> macPath;

                    AttrStage stage() const override {
                        return AttrStage::Pre;
                    }

                    bool runDuringIter() const override {
                        return false;
                    }

                    void handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t modIdx, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
                        if (!i.is_None()) {
                            auto lex = ProcMacroInvoke(sp, wb, crate, this->macPath, attr.data(), attrs, vis, path.nodes.back(), i);
                            if (lex) {
                                // TODO: `derive_where` returns its own attribute invocation in the output, between two other additions

                                i = ASTItem::make_None({});
                                lex->parseState().module = &mod;
                                ParseModRootItemsInto(mod, modIdx, *lex);
                                ExpandModExternCrates(wb, crate, mod.path(), mod, modIdx + 1);
                            } else {
                                ERROR(sp, E0000, "proc_macro expansion failed");
                            }
                        }
                    }

                    void handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
                        if (!i.is_None()) {
                            auto lex = ProcMacroInvoke(sp, wb, crate, this->macPath, attr.data(), attrs, vis, name, i);
                            if (lex) {
                                i = ASTItem::make_None({});
                                BUG_ASSERT(currentMod);
                                lex->parseState().module = currentMod;
                                while (lex->lookahead(0) != TOK_EOF) {
                                    ParseImplItem(*lex, impl);
                                }
                            } else {
                                ERROR(sp, E0000, "proc_macro expansion failed");
                            }
                        }
                    }

                    ASTModule* currentMod = nullptr;
                } d;

                if (stage == AttrStage::Pre) {
                    d.currentMod = es.currentMod;
                    d.macPath.push_back(procMac->path.crateName());
                    d.macPath.insert(d.macPath.end(), procMac->path.components().begin(), procMac->path.components().end());
                    f.run(sp, d, a);
                    a.markInert();
                }
                found = true;
            } else if (m.is_MacroRules()) {
            } else {
                TODO(sp, "Attr " << a.name() << " : " << m.tagStr());
            }
        }
        if (!found) {
            // TODO: Create no-op handlers for a whole heap of attributes

            //TODO(sp, "Unknown attribute #[" << a.name() << "]");
        }
    }

    void ExpandAttrs(const ExpandState& es, const ASTAttributeList& attrs, AttrStage stage, const ExpandAttrCallback& f) {
        for (auto& a : attrs.items) {
            const RcString rcstringCfg = RcString::newInterned("cfg");
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
            const RcString rcstringCfgAttr = RcString::newInterned("cfg_attr");
            if (a.name() == rcstringCfgAttr) {
                auto newAttrs = checkCfgAttr(settings, a);
                it = attrs.items.erase(it);
                it = attrs.items.insert(it, std::make_move_iterator(newAttrs.begin()), std::make_move_iterator(newAttrs.end()));
            } else {
                ++it;
            }
        }
    }

    void ExpandModExternCrates(const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& modpath, ASTModule& mod, unsigned int firstItem) {
        for (unsigned int idx = firstItem; idx < mod.items.size(); idx++) {
            auto& item = *mod.items[idx];
            auto* crateItem = item.data.opt_Crate();
            if (!crateItem) {
                continue;
            }

            ExpandAttrsCfgAttr(*wb.settings, item.attrs);
            if (!checkCfgAttrs(*wb.settings, item.attrs)) {
                continue;
            }

            if (crateItem->name != "") {
                if (crate.externCrates.count(crateItem->name) == 0) {
                    crateItem->name = crate.loadExternCrate(*wb.settings, item.span, crateItem->name);
                }
                if (modpath.nodes.empty()) {
                    wb.settings->implicitCrates.insert({item.name, crateItem->name});
                }
            } else if (modpath.nodes.empty()) {
                wb.settings->implicitCrates.insert({item.name, ""});
            }
        }
    }

    slice<const ASTAttribute> getAttrsAfter(const ASTAttributeList& attrs, const ASTAttribute& a) {
        const auto* start = &a + 1;
        const auto* end = &attrs.items.back() + 1;
        return slice<const ASTAttribute>(start, end - start);
    }

    void ExpandAttrs(const ExpandState& es, const ASTAttributeList& attrs, AttrStage stage, const ASTAbsolutePath& path, ASTModule& mod, size_t modIdx, const ASTVisibility& vis, ASTItem& item) {
        ExpandAttrs(es, attrs, stage, makeCallable<ExpandAttrCb>([&](const Span& sp, const ExpandDecorator& d, const ASTAttribute& a) {
            if (!item.is_None()) {
                auto attrsSlice = d.wantsAllAttrs() ? slice<const ASTAttribute>(attrs.items.data(), attrs.items.size()) : getAttrsAfter(attrs, a);
                d.handle(sp, a, es.wb, es.crate, path, mod, modIdx, attrsSlice, vis, item);
            }
        }));
    }

    void ExpandAttrs(const ExpandState& es, const ASTAttributeList& attrs, AttrStage stage, const ASTAbsolutePath& path, ASTModule& mod, ASTTrait& trait, ASTItem& item) {
        ExpandAttrs(es, attrs, stage, makeCallable<ExpandAttrCb>([&](const Span& sp, const ExpandDecorator& d, const ASTAttribute& a) {
            if (!item.is_None()) {
                d.handle(sp, a, es.wb, es.crate, path, trait, getAttrsAfter(attrs, a), item);
            }
        }));
    }

    void ExpandAttrs(const ExpandState& es, const ASTAttributeList& attrs, AttrStage stage, ASTModule& mod, ASTImpl& impl, const ASTVisibility& vis, const RcString& name, ASTItem& item) {
        ExpandAttrs(es, attrs, stage, makeCallable<ExpandAttrCb>([&](const Span& sp, const ExpandDecorator& d, const ASTAttribute& a) {
            if (!item.is_None()) {
                d.handle(sp, a, es.wb, es.crate, impl, name, getAttrsAfter(attrs, a), vis, item);
            }
        }));
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
            } else if (a.name() == "doc") {
            } else {
                TODO(sp, "non-cfg attributes - " << a);
            }
        }));
        return !remove;
    }

    std::unique_ptr<TokenStream> ExpandMacroInner(const WireBoard& wb, const ASTCrate& crate, LList<const ASTModule*> modstack, ASTModule& mod, Span miSpan, const ASTPath& path, const RcString& inputIdent, TokenTree& inputTt) {
        ASSERT_BUG(miSpan, path.isValid(), "Macro invocation with invalid path");

        TRACE_FUNCTION_F("Searching for macro " << path);
        MacroRef mac;
        if (inputIdent != "" && path.isTrivial() && path.asTrivial() == "macro_rules") {
            if (auto* pm = ExpandFindProcMacro(wb, path.asTrivial())) {
                mac = MacroRef(pm);
            }
        }
        if (mac.is_None()) {
            mac = ExpandLookupMacro(miSpan, wb, crate, modstack, path);
        }
        if (mac.is_MacroRules()) {
            // TODO: If `mr_ptr` is tagged with #[rustc_builtin_macro], look for a matching entry in `g_macros`
        }

        std::unique_ptr<TokenStream> rv;
        switch (mac.tag()) {
            case MacroRef::TAG_None: {
                DEBUG("Unknown macro " << path);
                return std::unique_ptr<TokenStream>();
            }
            case MacroRef::TAG_ExternalProcMacro: {
                auto& procMac = mac.as_ExternalProcMacro();
                std::vector<RcString> macPath;
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

    std::unique_ptr<TokenStream> ExpandMacro(const ExpandState& es, ASTModule& mod, Span miSpan, const ASTPath& path, const RcString& inputIdent, TokenTree& inputTt) {
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

    std::unique_ptr<TokenStream> ExpandMacro(const ExpandState& es, ASTModule& mod, ASTMacroInvocation& mi) {
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
        auto expandNodes = [&](std::vector<ASTPathNode>& nodes) {
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

    static ASTPath getPath(const RcString& coreCrate, const char* c1, const char* c2) {
        return ASTAbsolutePath(coreCrate, {RcString::newInterned(c1), RcString::newInterned(c2)});
    }

    static ASTPath getPath(const RcString& coreCrate, const char* c1, const char* c2, const char* c3) {
        return ASTAbsolutePath(coreCrate, {RcString::newInterned(c1), RcString::newInterned(c2), RcString::newInterned(c3)});
    }

    void ExpandExpr(const ExpandState& es, ASTExprNodeP& node) {
        TRACE_FUNCTION_F("unique_ptr");
        CExpandExpr visitor{es};
        visitor.visit(node);
    }

    void ExpandExpr(const ExpandState& es, ASTExprNode*& node) {
        TRACE_FUNCTION_F("shared_ptr");
        CExpandExpr visitor{es};
        node->visit(visitor);
        if (visitor.replacement) {
            node = visitor.replacement.release();
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
                    ExpandType(es, mod, be.type);
                    ExpandPath(es, mod, be.trait);
                    break;
                }
                case ASTGenericBound::TAG_MaybeTrait: {
                    auto& be = bound.as_MaybeTrait();
                    ExpandType(es, mod, be.type);
                    ExpandPath(es, mod, be.trait);
                    break;
                }
                case ASTGenericBound::TAG_NotTrait: {
                    auto& be = bound.as_NotTrait();
                    ExpandType(es, mod, be.type);
                    ExpandPath(es, mod, be.trait);
                    break;
                }
                case ASTGenericBound::TAG_Equality: {
                    auto& be = bound.as_Equality();
                    ExpandType(es, mod, be.type);
                    ExpandType(es, mod, be.replacement);
                    break;
                }
            }
        }
        for (auto& t : params.bareBoundTypes) {
            ExpandType(es, mod, t);
        }
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
                        auto miOwned = mv$(e);

                        auto ttl = ExpandMacro(es, mod, miOwned);

                        if (ttl) {
                            while (ttl->lookahead(0) != TOK_EOF) {
                                ParseImplItem(*ttl, impl);
                            }
                            ASSERT_BUG(miOwned.span(), impl.items().size() > idx, "");

                            *i.data = ASTItem::make_None({});
                        } else {
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
                    auto miOwned = mv$(e);

                    if (!miOwned.isExpanded()) {
                        BUG_ASSERT(miOwned.span());

                        TRACE_FUNCTION_F("Macro invoke " << miOwned.path());
                        auto ttl = ExpandMacro(es, mod, miOwned);
                        if (ttl) {
                            // TODO: What if this attribute adds new items? Or if it changes the type?

                            ExpandAttrs(es, attrs, AttrStage::Post, path, mod, idx, vis, dat);

                            // TODO: All new items should be placed just after this?
                            DEBUG("-- Parsing as extern block items");
                            auto ipos = block.items().begin() + idx;
                            while (!ttl->getTokenIf(TOK_EOF)) {
                                ipos = block.items().insert(ipos + 1, ParseExternBlockItem(*ttl, block.abi()));
                            }

                            miOwned.setExpanded();
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
        ExpandModExternCrates(es.wb, es.crate, modpath, mod, firstItem);

        // TODO: Pre-parse all macro_rules invocations into items?

        if (es.mode == ExpandMode::FirstPass) {
            if (firstItem == 0) {
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

            if (es.crate.preludePath != ASTPath()) {
                if (mod.insertPrelude && !mod.isAnon()) {
                    DEBUG("> Adding custom prelude " << es.crate.preludePath);
                    mod.addItem(Span(), ASTVisibility::makeRestricted(ASTVisibility::Ty::Private, mod.path()), "", ASTUseItem{Span(), true, ::makeVec1(ASTUseItem::Ent{Span(), es.crate.preludePath, ""})}, {});
                    DEBUG("> Not inserting custom prelude (anon or disabled)");
                }
            }
        }

        std::vector<const ASTNamed<ASTItem>*> macroRecursionStack;

        DEBUG("Items");
        for (unsigned int idx = firstItem; idx < mod.items.size(); idx++) {
            auto& i = *mod.items[idx];

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

            {
                struct H {
                    static void filterCfg(const Settings& settings, std::vector<ASTStructItem>& lst) {
                        auto newEnd = std::remove_if(lst.begin(), lst.end(), [&](const ASTStructItem& v) {
                            return !checkCfgAttrs(settings, v.attrs);
                        });
                        DEBUG(lst.size() << " -> " << newEnd - lst.begin());
                        lst.erase(newEnd, lst.end());
                    }

                    static void filterCfg(const Settings& settings, std::vector<ASTTupleItem>& lst) {
                        auto newEnd = std::remove_if(lst.begin(), lst.end(), [&](const ASTTupleItem& v) {
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

                                ++it;
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

            if (i.data.is_Module()) {
                auto& e = i.data.as_Module();
                LList<const ASTModule*> subModstack(&es.modstack, &e);
                ExpandState esInner(es.wb, es.crate, subModstack, es.mode, &e);
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

                    if (macroRecursionStack.size() > es.wb.settings->recursionLimit) {
                        ERROR(i.span, E0000, "Exceeded macro recusion limit of " << es.wb.settings->recursionLimit);
                    }
                    auto miOwned = mv$(e);

                    if (!miOwned.isExpanded()) {
                        BUG_ASSERT(miOwned.span());

                        TRACE_FUNCTION_F("Macro invoke " << miOwned.path());
                        auto ttl = ExpandMacro(es, mod, miOwned);
                        if (ttl) {
                            DEBUG("-- Parsing as mod items");
                            size_t oldLen = mod.items.size();
                            ParseModRootItemsInto(mod, idx, *ttl);
                            ExpandModExternCrates(es.wb, es.crate, modpath, mod, idx + 1);

                            auto nextNonMacroItem = idx + 1 + (mod.items.size() - oldLen);
                            macroRecursionStack.push_back(nextNonMacroItem == mod.items.size() ? nullptr : &*mod.items[nextNonMacroItem]);

                            miOwned.setExpanded();
                        } else {
                            DEBUG("Deferred macro");
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
                    for (const auto& ue : e.entries) {
                        if (ue.name != "" && ue.path.nodes().size() >= 1) {
                            DEBUG("Use " << ue.path);
                            ASTAbsolutePath refPath;
                            auto m = ResolveLookupMacro(ue.sp, *es.wb.settings, es.crate, mod.path(), ue.path, /*out_path=*/&refPath);
                            MacroRef ref;
                            switch (m.tag()) {
                                case ResolveItemRefMacro::TAG_None: {
                                    break;
                                }
                                case ResolveItemRefMacro::TAG_InternalMacro: {
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
                            if (!ref.is_None()) {
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
                    UNREACHABLE();
                }
                case ASTItem::TAG_Crate: {
                    auto& e = dat.as_Crate();
                    if (e.name != "") {
                        if (es.crate.externCrates.count(e.name) == 0) {
                            e.name = es.crate.loadExternCrate(*es.wb.settings, i.span, e.name);
                        }
                    }
                    if (modpath.nodes.empty()) {
                        es.wb.settings->implicitCrates.insert_or_assign(i.name, e.name);
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
                        ExpandExpr(es, var.discriminantValue);
                        ExpandAttrs(es, var.attrs, AttrStage::Post, makeCallable<ExpandAttrCb>([&](const Span& sp, const auto& d, const auto& a) {
                            d.handle(sp, a, es.wb, es.crate, var);
                        }));
                    }
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
                                    auto miOwned = mv$(e);

                                    auto ttl = ExpandMacro(es, mod, miOwned);

                                    if (ttl.get()) {
                                        size_t insertPos = idx + 1;
                                        while (ttl->lookahead(0) != TOK_EOF) {
                                            auto i = ParseTraitItem(*ttl);
                                            traitItems.insert(traitItems.begin() + insertPos, mv$(i));
                                            insertPos++;
                                        }
                                        traitItems[idx].data = ASTItem::make_None({});
                                    } else {
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
            ExpandAttrs(es, attrs, AttrStage::Post, path, mod, idx, vis, dat);

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

                    DEBUG("macro item export: " << i->name);
                    i->data.as_Macro()->exported = true;
                } else {
                    ERROR(i->span, E0000, "#[macro_export] on non-macro_rules - " << i->data.tagStr());
                }
            } else if (auto* e = i->data.opt_Module()) {
                Expand_Mod_Early(wb, crate, *e, newRootItems);
            }
        }

        DEBUG("Items");
        for (unsigned int idx = 0; idx < mod.items.size(); idx++) {
            auto& i = *mod.items[idx];
            if (auto* mi = i.data.opt_MacroInv()) {
                // 1.74 HACK - Parse `macro_rules` during the first pass, so they're present for `use` to refer to
                if (mi->path().isTrivial() && mi->path().asTrivial() == "macro_rules") {
                    if (i.attrs.get("rustc_builtin_macro")) {
                        continue;
                    }
                    if (mi->inputIdent() == "") {
                        continue;
                    }
                    auto miOwned = mv$(*mi);

                    TRACE_FUNCTION_F("Macro invoke " << miOwned.path());
                    ExpandState es{wb, crate, {}, ExpandMode::Iterate, &mod};
                    auto ttl = ExpandMacro(es, mod, miOwned);
                    ASSERT_BUG(miOwned.span(), ttl, "BUG: macro_rules not expanded");
                    BUG_ASSERT(miOwned.path().isValid());

                    if (ttl.get()) {
                        BUG_ASSERT(ttl.get());
                        DEBUG("-- Parsing as mod items");
                        ParseModRootItemsInto(mod, idx, *ttl);
                    } else {
                    }
                    mod.items[idx]->data.as_MacroInv() = mv$(miOwned);
                }
            }
        }
    }
}

void ExpandRegistry::addDecorator(const char* name, ExpandDecorator* handler) {
    decorators = pool->make<DecoratorEntry>(DecoratorEntry{name, handler, decorators});
}

void ExpandRegistry::addMacro(const char* name, ExpandProcMacro* handler) {
    macros = pool->make<MacroEntry>(MacroEntry{name, handler, macros});
}

ExpandProcMacro* ExpandRegistry::findMacro(const RcString& name) const {
    for (auto* entry = macros; entry; entry = entry->next) {
        if (name == entry->name) {
            return entry->handler;
        }
    }
    return nullptr;
}

ExpandDecorator* ExpandRegistry::findDecorator(const RcString& name) const {
    for (auto* entry = decorators; entry; entry = entry->next) {
        if (name == entry->name) {
            return entry->handler;
        }
    }
    return nullptr;
}

void ExpandInit(ExpandRegistry& registry) {
    RegisterBuiltinDecorators(registry);
    RegisterBuiltinMacros(registry);
    RegisterSynextBuiltins(registry);
    RegisterCfgBuiltins(registry);
    RegisterProcMacroBuiltins(registry);
}

void ExpandDecorator::unexpected(const Span& sp, const ASTAttribute& mi, const char* locStr) const {
    WARNING(sp, W0000, "Unexpected attribute " << mi.name() << " on " << locStr);
}

ExpandProcMacro* ExpandFindProcMacro(const WireBoard& wb, const RcString& name) {
    return wb.expandRegistry->findMacro(name);
}

ExpandDecorator* ExpandFindDecorator(const WireBoard& wb, const RcString& name) {
    return wb.expandRegistry->findDecorator(name);
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
                        if (definition.filename == invocation.filename && (definition.startLine > invocation.startLine || (definition.startLine == invocation.startLine && definition.startOfs > invocation.startOfs))) {
                            DEBUG(macMod.path() << "::" << mr.name << " - Defined later, skipping");
                            continue;
                        }
                    }
                    DEBUG(macMod.path() << "::" << mr.name << " - Defined");
                    return MacroRef(&*mr.data);
                }
            }

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
        if (auto* pm = ExpandFindProcMacro(wb, name)) {
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
        if (auto* pm = ExpandFindProcMacro(wb, name)) {
            return MacroRef(pm);
        }
    }

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

void ExpandBareExpr(const WireBoard& wb, const ASTCrate& crate, const ASTModule& mod, ASTExprNodeP& node) {
    ExpandState es{wb, const_cast<ASTCrate&>(crate), LList<const ASTModule*>(nullptr, &mod), ExpandMode::FirstPass, const_cast<ASTModule*>(&mod)};
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

void Expand(const WireBoard& wb, ASTCrate& crate) {
    wb.expandRegistry->eachDecorator([](const char* name, const ExpandDecorator&) {});
    wb.expandRegistry->eachMacro([](const char* name, const ExpandProcMacro&) {});

    ExpandState es{wb, crate, LList<const ASTModule*>(nullptr, &crate.rootModule_), ExpandMode::FirstPass, &crate.rootModule_};

    ExpandAttrsCfgAttr(*es.wb.settings, crate.attrs);
    ExpandAttrs(es, crate.attrs, AttrStage::Pre, makeCallable<ExpandAttrCb>([&](const Span& sp, const auto& d, const auto& a) {
        d.handle(sp, a, es.wb, crate);
    }));

    // TODO: Crate name and type

    std::vector<std::unique_ptr<ASTNamed<ASTItem>>> newRootItems;
    Expand_Mod_Early(wb, crate, crate.rootModule_, newRootItems);
    crate.rootModule_.items.insert(crate.rootModule_.items.begin(), std::make_move_iterator(newRootItems.begin()), std::make_move_iterator(newRootItems.end()));

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
        crate.rootModule_.items.push_back(box$(ASTNamed<ASTItem>(Span(), mv$(attrs), ASTVisibility::makeRestricted(ASTVisibility::Ty::Private, ASTAbsolutePath()), stdCrateShortname, ASTItem::make_Crate({stdCrateName}))));
        auto& i = *crate.rootModule_.items.back();
        ExpandAttrs(es, i.attrs, AttrStage::Post, ASTAbsolutePath(), crate.rootModule_, 0, i.vis, i.data);
    }

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

    ExpandModIndexAnon(crate, crate.rootModule_);

    {
        auto& exportedMacros = crate.exportedMacros;

        std::vector<ASTModule*> mods;
        mods.push_back(&crate.rootModule_);
        do {
            auto& mod = *mods.back();
            mods.pop_back();

            for (/*const*/ auto& mac : mod.macros()) {
                if (mac.data->exported) {
                    exportedMacros.insert(std::make_pair(mac.name, &*mac.data));
                    DEBUG("- Non-exported " << mac.name << "!");
                }
            }

            for (auto& i : mod.items) {
                if (i->data.is_Module()) {
                    mods.push_back(&i->data.as_Module());
                }
            }
        } while (mods.size() > 0);

        for (const auto& mac : crate.rootModule_.macroImports) {
            if (mac.isPub) {
                if (!mac.ref.is_MacroRules()) {
                    continue;
                }
                auto v = std::make_pair(mac.name, mac.ref.as_MacroRules());

                auto it = exportedMacros.find(mac.name);
                if (it == exportedMacros.end()) {
                    exportedMacros.insert(mv$(v));
                    DEBUG("- Import " << mac.name << "! (from \"" << res.first->second->sourceCrate << "\")");
                } else if (v.second->rules.empty()) {
                } else {
                    DEBUG("- Replace " << mac.name << "! (from \"" << it->second->sourceCrate << "\") with one from \"" << v.second->sourceCrate << "\"");
                    it->second = mv$(v.second);
                }
            }
        }
    }
}

ExpandState::ExpandState(const WireBoard& wb, ASTCrate& crate, LList<const ASTModule*> modstack, ExpandMode mode, ASTModule* currentMod)
    : wb(wb)
    , crate(crate)
    , modstack(modstack)
    , mode(mode)
    , currentMod(currentMod)
    , change(false)
    , hasMissing(false)
{
    DEBUG("" << this);
}

template <typename F>
ExpandAttrCb<F>::ExpandAttrCb(F f)
    : f(f)
{
}

template <typename F>
auto ExpandAttrCb<F>::run(const Span& sp, const ExpandDecorator& d, const ASTAttribute& a) const -> void {
    f(sp, d, a);
}

CExpandExpr::CExpandExpr(const ExpandState& es)
    : crate(es.crate)
    , parentExpandState(es)
    , expandState(es)
{
}

CExpandExpr::~CExpandExpr() {
    if (expandState.change) {
        parentExpandState.change = true;
    }
    if (expandState.hasMissing) {
        parentExpandState.hasMissing = true;
    }
}

auto CExpandExpr::curMod() -> ASTModule& {
    return *const_cast<ASTModule*>(expandState.modstack.item);
}

auto CExpandExpr::visit(ASTExprNodeP& cnode) -> void {
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
    BUG_ASSERT(!this->replacement);
}

auto CExpandExpr::visitNodelete(const ASTExprNode& parent, ASTExprNodeP& cnode) -> void {
    if (cnode.get() != nullptr) {
        this->visit(cnode);
        if (cnode.get() == nullptr) {
            ERROR(parent.span(), E0000, "#[cfg] not allowed in this position");
        }
    }
    BUG_ASSERT(!this->replacement);
}

auto CExpandExpr::visitVector(std::vector<ASTExprNodeP>& cnodes) -> void {
    for (auto it = cnodes.begin(); it != cnodes.end();) {
        BUG_ASSERT(it->get());
        this->visit(*it);
        if (it->get() == nullptr) {
            it = cnodes.erase(it);
        } else {
            ++it;
        }
    }
}

auto CExpandExpr::visitMacro(ASTExprNodeMacro& node, std::vector<ASTExprNodeBlock::Line>* nodesOut) -> ASTExprNodeP {
    TRACE_FUNCTION_F(node.path << "!");
    if (!node.path.isValid()) {
        return ASTExprNodeP();
    }

    const bool definesMacro = node.path.isTrivial() && node.path.asTrivial() == "macro_rules";

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
        DEBUG("Deferred");
    } else {
        if (definesMacro) {
            auto it = std::find_if(mod.macros().rbegin(), mod.macros().rend(), [&](const auto& macro) {
                return macro.name == node.ident;
            });
            ASSERT_BUG(node.span(), it != mod.macros().rend(), "macro_rules! definition was not installed");
            it->data->definitionHygiene = node.definitionHygiene;
            if (nodesOut) {
                auto marker = makeNode<ASTExprNodeMacroDefinition>(it->data->definitionId, it->data->hygiene, it->data->definitionHygiene);
                marker->setSpan(node.span());
                nodesOut->push_back({true, std::move(marker)});
            }
        }
        if (!nodesOut) {
            if (ttl->lookahead(0) != TOK_EOF) {
                SET_MODULE((*ttl), mod);
                DEBUG("-- Parsing as expression");
                rv = ParseExpr0(*ttl);
                if (ttl->lookahead(0) == TOK_SEMICOLON && ttl->lookahead(1) == TOK_EOF) {
                    ttl->getToken();
                }
                if (ttl->lookahead(0) != TOK_EOF) {
                    ERROR(node.span(), E0000, "Unused tokens at the end of macro expansion - " << ttl->getToken());
                }
            } else if (ttl->isMacroExpansionPlaceholder()) {
                rv = makeNode<ASTExprNodeTuple>(decltype(ASTExprNodeTuple::values)());
                rv->setSpan(node.span());
            }
        } else {
            while (ttl->lookahead(0) != TOK_EOF) {
                SET_MODULE((*ttl), mod);

                bool addSilenceIfEnd = false;
                std::shared_ptr<ASTModule> tmpLocalMod;
                auto& localModPtr = (this->currentBlock ? this->currentBlock->localMod : tmpLocalMod);
                DEBUG("-- Parsing as statement line");
                auto newexpr = ParseExprBlockLineWithItems(*ttl, localModPtr, addSilenceIfEnd);

                if (tmpLocalMod) {
                    TODO(node.span(), "Handle edge case where a macro expansion outside of a _Block creates an item");
                }

                if (newexpr) {
                    nodesOut->push_back({addSilenceIfEnd, mv$(newexpr)});
                } else {
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

auto CExpandExpr::visit(ASTExprNodeMacro& node) -> void {
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
            BUG_ASSERT(!this->replacement);
            this->replacement = mv$(n);
        }
    }
}

auto CExpandExpr::visit(ASTExprNodeBlock& node) -> void {
    this->inAssignLhs = false;
    unsigned int modItemCount = 0;
    bool hasLocalMod = node.localMod != nullptr;

    auto prevModstack = this->expandState.modstack;
    if (node.localMod) {
        this->expandState.modstack = LList<const ASTModule*>(&prevModstack, node.localMod.get());
    }

    // TODO: macro_rules! invocations within the expression list influence this.

    if (node.localMod) {
        ExpandMod(this->expandState, node.localMod->path(), *node.localMod);
        modItemCount = node.localMod->items.size();
    }

    auto saved = this->currentBlock;
    this->currentBlock = &node;

    for (auto it = node.nodes.begin(); it != node.nodes.end();) {
        BUG_ASSERT(it->node.get());

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

            BUG_ASSERT(it->node.get() == nodeMac);

            std::vector<ASTExprNodeBlock::Line> newNodes;
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
                DEBUG("++ " << *n.node << (n.hasSemicolon ? " ;" : ""));
            }
            if (nodeMac->path.isValid()) {
                DEBUG("Deferred macro");
                ++it;
            } else {
                if (it->hasSemicolon && !newNodes.empty()) {
                    newNodes.back().hasSemicolon = true;
                }
                it = node.nodes.erase(it);
                it = node.nodes.insert(it, std::make_move_iterator(newNodes.begin()), std::make_move_iterator(newNodes.end()));
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

auto CExpandExpr::visit(ASTExprNodeAsyncBlock& node) -> void {
    this->visitNodelete(node, node.inner);
}

auto CExpandExpr::visit(ASTExprNodeGeneratorBlock& node) -> void {
    ExpandType(this->expandState, this->curMod(), node.returnType);
    this->visitNodelete(node, node.inner);
}

auto CExpandExpr::visit(ASTExprNodeTry& node) -> void {
    if (expandState.mode != ExpandMode::Final) {
        this->visitNodelete(node, node.inner);
        return;
    }

    tryStack.push_back(RcString::newInterned(FMT("#try" << tryIndex++)));
    this->visitNodelete(node, node.inner);
    auto loopName = mv$(tryStack.back());
    tryStack.pop_back();

    auto coreCrate = crate.extCratenameCore;
    auto pathTry = getPath(coreCrate, "ops", "Try");
    auto pathTryFromOutput = ASTPath::newUfcsTrait(::mkType(*parentExpandState.crate.pool, node.span()), pathTry, {ASTPathNode(RcString::newInterned("from_output"))});
    auto okNode = makeNode<ASTExprNodeCallPath>(mv$(pathTryFromOutput), ::makeVec1(mv$(node.inner)));
    auto breakNode = makeNode<ASTExprNodeFlow>(ASTExprNodeFlow::BREAK, loopName, mv$(okNode));
    this->replacement = makeNode<ASTExprNodeLoop>(loopName, mv$(breakNode));
}

auto CExpandExpr::visit(ASTExprNodeAsm& node) -> void {
    for (auto& v : node.output) {
        this->visitNodelete(node, v.value);
    }
    for (auto& v : node.input) {
        this->visitNodelete(node, v.value);
    }
}

auto CExpandExpr::visit(ASTExprNodeAsm2& node) -> void {
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

auto CExpandExpr::visit(ASTExprNodeFlow& node) -> void {
    this->visitNodelete(node, node.value);

    if (node.type == ASTExprNodeFlow::YEET) {
        if (expandState.mode != ExpandMode::Final) {
            return;
        }

        auto coreCrate = crate.extCratenameCore;
        auto pathOpsYeet = getPath(coreCrate, "ops", "Yeet");
        auto pathFromResidualFromResidual = getPath(coreCrate, "ops", "FromResidual", "from_residual");

        auto yeeted = std::move(node.value);
        if (!yeeted) {
            yeeted = makeNode<ASTExprNodeTuple>(decltype(ASTExprNodeTuple::values)());
            yeeted->setSpan(node.span());
        }
        auto v = makeNode<ASTExprNodeCallPath>(ASTPath(pathOpsYeet), ::makeVec1(std::move(yeeted)));
        v->setSpan(node.span());
        v = makeNode<ASTExprNodeCallPath>(ASTPath(pathFromResidualFromResidual), ::makeVec1(std::move(v)));
        v->setSpan(node.span());
        replacement = makeNode<ASTExprNodeFlow>((tryStack.empty() ? ASTExprNodeFlow::RETURN : ASTExprNodeFlow::BREAK), (tryStack.empty() ? RcString("") : tryStack.back()), std::move(v));
        replacement->setSpan(node.span());
    }
}

auto CExpandExpr::visit(ASTExprNodeLetBinding& node) -> void {
    ExpandType(this->expandState, this->curMod(), node.type);
    ExpandPattern(this->expandState, this->curMod(), node.pat, false);
    this->visitNodelete(node, node.value);
    this->visitNodelete(node, node.elseNode);
}

auto CExpandExpr::visit(ASTExprNodeAssign& node) -> void {
    inAssignLhs = true;
    this->visitNodelete(node, node.slot);
    inAssignLhs = false;
    this->visitNodelete(node, node.value);

    if (node.op == ASTExprNodeAssign::NONE) {
        struct VisitorToPat: public ASTNodeVisitor {
            std::vector<std::pair<RcString, ASTExprNodeP>> slots;
            ASTPattern rv;

            bool rvSet = false;
            bool isSlot = false;

            ASTPattern lower(ASTExprNodeP& ep) {
                BUG_ASSERT(ep);
                ep->visit(*this);
                ASSERT_BUG(ep->span(), rvSet, ep.typeName() << " - Didn't yield a pattern");
                if (isSlot) {
                    BUG_ASSERT(!slots.empty());
                    BUG_ASSERT(!slots.back().second);
                    slots.back().second = std::move(ep);
                    isSlot = false;
                }
                rvSet = false;
                return std::move(rv);
            }

            void pat(ASTPattern rv) {
                BUG_ASSERT(!rvSet);
                BUG_ASSERT(!isSlot);
                rvSet = true;
                BUG_ASSERT(rv.bindings().empty());
                this->rv = std::move(rv);
            }

            void slot(ASTExprNode& v) {
                rvSet = true;
                isSlot = true;

                RcString name(FMT("_#" << slots.size()).c_str());
                slots.push_back(std::make_pair(name, ASTExprNodeP()));
                rv = ASTPattern(ASTPattern::TagBind(), v.span(), slots.back().first);
            }

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

            static bool isRest(ASTExprNodeP& ep) {
                if (const auto* e = cast<ASTExprNodeBinOp>(ep.get())) {
                    return e->type == ASTExprNodeBinOp::RANGE && !e->left && !e->right && !e->parenthesised;
                }
                return false;
            }

            ASTPattern::TuplePat lowerTuplePat(const Span& sp, std::vector<ASTExprNodeP>& values) {
                bool isSplit = false;
                std::vector<ASTPattern> start;
                std::vector<ASTPattern> end;
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
            BUG_ASSERT(pat.bindings().size() == 1);
            BUG_ASSERT(!node.slot);
            BUG_ASSERT(v.slots.front().second);
            node.slot = std::move(v.slots.front().second);
        } else {
            auto rv = makeNode<ASTExprNodeBlock>();
            static_cast<ASTExprNodeBlock&>(*rv).nodes.push_back({true, makeNode<ASTExprNodeLetBinding>(std::move(pat), mkType(*parentExpandState.crate.pool, node.span()), std::move(node.value))});
            for (auto& slots : v.slots) {
                static_cast<ASTExprNodeBlock&>(*rv).nodes.push_back({true, makeNode<ASTExprNodeAssign>(ASTExprNodeAssign::NONE, std::move(slots.second), makeNode<ASTExprNodeNamedValue>(ASTPath::newLocal(std::move(slots.first))))});
            }
            this->replacement = std::move(rv);
        }
    }
}

auto CExpandExpr::visit(ASTExprNodeCallPath& node) -> void {
    ExpandPath(this->expandState, this->curMod(), node.path);
    this->visitVector(node.args);
}

auto CExpandExpr::visit(ASTExprNodeCallMethod& node) -> void {
    ExpandPathParams(this->expandState, this->curMod(), node.method.args());
    this->visitNodelete(node, node.val);
    this->visitVector(node.args);
}

auto CExpandExpr::visit(ASTExprNodeCallObject& node) -> void {
    this->visitNodelete(node, node.val);
    this->visitVector(node.args);
}

auto CExpandExpr::visit(ASTExprNodeLoop& node) -> void {
    this->visitNodelete(node, node.code);
}

auto CExpandExpr::visit(ASTExprNodeFor& node) -> void {
    ExpandPattern(this->expandState, this->curMod(), node.pattern, false);
    this->visitNodelete(node, node.value);
    this->visitNodelete(node, node.code);

    const RcString rcstringIntoIter = RcString::newInterned("into_iter");
    const RcString rcstringIntoAsyncIter = RcString::newInterned("into_async_iter");
    const RcString rcstringNext = RcString::newInterned("next");
    const RcString rcstringIt = RcString::newInterned("it");
    const auto iteratorHygiene = Ident::Hygiene::newScope(parentExpandState.wb.id, *parentExpandState.crate.hirPool);
    auto coreCrate = crate.extCratenameCore;
    auto pathSome = getPath(coreCrate, "option", "Option", "Some");
    auto pathNone = getPath(coreCrate, "option", "Option", "None");
    auto pathIntoIterator = node.isAwait ? getPath(coreCrate, "async_iter", "IntoAsyncIterator") : getPath(coreCrate, "iter", "IntoIterator");
    auto pathIterator = getPath(coreCrate, "iter", "Iterator");
    std::vector<ASTExprNodeMatchArm> arms;
    arms.push_back(ASTExprNodeMatchArm(::makeVec1(ASTPattern(ASTPattern::TagNamedTuple(), node.span(), pathSome, ::makeVec1(mv$(node.pattern)))), {}, mv$(node.code)));
    arms.push_back(ASTExprNodeMatchArm(::makeVec1(ASTPattern(ASTPattern::TagValue(), node.span(), ASTPattern::Value::make_Named(pathNone))), {}, makeNode<ASTExprNodeFlow>(ASTExprNodeFlow::BREAK, node.label, nullptr)));

    auto nextReceiver = makeNode<ASTExprNodeNamedValue>(ASTPath::newRelative(iteratorHygiene, ::makeVec1(ASTPathNode(rcstringIt))));
    auto nextCall = node.isAwait ? makeNode<ASTExprNodeUniOp>(ASTExprNodeUniOp::AWaitNext, mv$(nextReceiver)) : makeNode<ASTExprNodeCallPath>(ASTPath::newUfcsTrait(::mkType(*parentExpandState.crate.pool, node.span()), pathIterator, {ASTPathNode(rcstringNext)}), ::makeVec1(makeNode<ASTExprNodeUniOp>(ASTExprNodeUniOp::REFMUT, mv$(nextReceiver))));
    auto nextMatch = makeNode<ASTExprNodeMatch>(mv$(nextCall), mv$(arms));
    auto loop = makeNode<ASTExprNodeLoop>(node.label, mv$(nextMatch));

    auto intoIterCall = makeNode<ASTExprNodeCallPath>(ASTPath::newUfcsTrait(::mkType(*parentExpandState.crate.pool, node.span()), pathIntoIterator, {ASTPathNode(node.isAwait ? rcstringIntoAsyncIter : rcstringIntoIter)}), ::makeVec1(mv$(node.value)));
    auto outerMatch = makeNode<ASTExprNodeMatch>(mv$(intoIterCall), ::makeVec1(ASTExprNodeMatchArm(::makeVec1(ASTPattern(ASTPattern::TagBind(), node.span(), Ident(iteratorHygiene, rcstringIt))), {}, mv$(loop))));

    auto block = makeNode<ASTExprNodeBlock>();
    static_cast<ASTExprNodeBlock&>(*block).nodes.push_back({true, mv$(outerMatch)});
    replacement = std::move(block);
    replacement->setSpan(node.span());
}

auto CExpandExpr::visit(ASTExprNodeWhile& node) -> void {
    for (auto& cond : node.conditions) {
        if (cond.optPat) {
            ExpandPattern(this->expandState, this->curMod(), *cond.optPat, true);
        }
        this->visitNodelete(node, cond.value);
    }
    this->visitNodelete(node, node.code);
}

auto CExpandExpr::liftGuardPatterns(ASTPattern& pat, std::vector<ASTIfLetCondition>& out) -> void {
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

auto CExpandExpr::visit(ASTExprNodeMatch& node) -> void {
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
            std::vector<ASTIfLetCondition> patternGuards;
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
    for (auto it = node.arms.begin(); it != node.arms.end();) {
        if (it->patterns.size() == 0) {
            it = node.arms.erase(it);
        } else {
            ++it;
        }
    }
}

auto CExpandExpr::visit(ASTExprNodeIf& node) -> void {
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

auto CExpandExpr::visit(ASTExprNodeWildcardPattern& node) -> void {
}

auto CExpandExpr::visit(ASTExprNodeInteger& node) -> void {
}

auto CExpandExpr::visit(ASTExprNodeFloat& node) -> void {
}

auto CExpandExpr::visit(ASTExprNodeBool& node) -> void {
}

auto CExpandExpr::visit(ASTExprNodeString& node) -> void {
}

auto CExpandExpr::visit(ASTExprNodeByteString& node) -> void {
}

auto CExpandExpr::visit(ASTExprNodeCString& node) -> void {
}

auto CExpandExpr::visit(ASTExprNodeSuffixedLiteral& node) -> void {
}

auto CExpandExpr::visit(ASTExprNodeClosure& node) -> void {
    auto tryStack = std::move(this->tryStack);
    for (auto& arg : node.args) {
        ExpandPattern(this->expandState, this->curMod(), arg.first, false);
        ExpandType(this->expandState, this->curMod(), arg.second);
    }
    ExpandType(this->expandState, this->curMod(), node.returnType);
    this->visitNodelete(node, node.code);
    this->tryStack = std::move(tryStack);
}

auto CExpandExpr::visit(ASTExprNodeStructLiteral& node) -> void {
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

auto CExpandExpr::visit(ASTExprNodeStructLiteralPattern& node) -> void {
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

auto CExpandExpr::visit(ASTExprNodeArray& node) -> void {
    this->visitNodelete(node, node.size);
    this->visitVector(node.values);
}

auto CExpandExpr::visit(ASTExprNodeTuple& node) -> void {
    this->visitVector(node.values);
}

auto CExpandExpr::visit(ASTExprNodeNamedValue& node) -> void {
    ExpandPath(this->expandState, this->curMod(), node.path);
}

auto CExpandExpr::visit(ASTExprNodeField& node) -> void {
    this->visitNodelete(node, node.obj);
}

auto CExpandExpr::visit(ASTExprNodeIndex& node) -> void {
    this->inAssignLhs = false;
    this->visitNodelete(node, node.obj);
    this->visitNodelete(node, node.idx);
}

auto CExpandExpr::visit(ASTExprNodeDeref& node) -> void {
    this->visitNodelete(node, node.value);
}

auto CExpandExpr::visit(ASTExprNodeCast& node) -> void {
    this->visitNodelete(node, node.value);
    ExpandType(this->expandState, this->curMod(), node.type);
}

auto CExpandExpr::visit(ASTExprNodeTypeAnnotation& node) -> void {
    this->visitNodelete(node, node.value);
    ExpandType(this->expandState, this->curMod(), node.type);
}

auto CExpandExpr::visit(ASTExprNodeBinOp& node) -> void {
    this->visitNodelete(node, node.left);
    this->visitNodelete(node, node.right);

    if (this->inAssignLhs) {
        return;
    }
    const RcString rcstringStart = RcString::newInterned("start");
    const RcString rcstringEnd = RcString::newInterned("end");
    const RcString rcstringNewRange = RcString::newInterned("new_range");
    const bool newRange = crate.features.count(rcstringNewRange) != 0;
    switch (node.type) {
        case ASTExprNodeBinOp::RANGE: {
            auto coreCrate = crate.extCratenameCore;
            auto pathRange = getPath(coreCrate, newRange ? "range" : "ops", "Range");
            auto pathRangeFrom = getPath(coreCrate, newRange ? "range" : "ops", "RangeFrom");
            auto pathRangeTo = getPath(coreCrate, "ops", "RangeTo");
            auto pathRangeFull = getPath(coreCrate, "ops", "RangeFull");

            ASTExprNodeStructLiteral::tValues values;
            if (node.left && node.right) {
                values.push_back({{}, rcstringStart, mv$(node.left)});
                values.push_back({{}, rcstringEnd, mv$(node.right)});
                replacement = makeNode<ASTExprNodeStructLiteral>(mv$(pathRange), nullptr, mv$(values));
            } else if (node.left) {
                values.push_back({{}, rcstringStart, mv$(node.left)});
                replacement = makeNode<ASTExprNodeStructLiteral>(mv$(pathRangeFrom), nullptr, mv$(values));
            } else if (node.right) {
                values.push_back({{}, rcstringEnd, mv$(node.right)});
                replacement = makeNode<ASTExprNodeStructLiteral>(mv$(pathRangeTo), nullptr, mv$(values));
            } else {
                replacement = makeNode<ASTExprNodeStructLiteral>(mv$(pathRangeFull), nullptr, mv$(values));
            }
            replacement->setSpan(node.span());
            break;
        }
        case ASTExprNodeBinOp::RANGE_INC: {
            auto coreCrate = crate.extCratenameCore;
            auto pathRangeInclusiveNonEmpty = getPath(coreCrate, newRange ? "range" : "ops", "RangeInclusive");
            auto pathRangeToInclusive = getPath(coreCrate, "ops", "RangeToInclusive");

            if (node.left) {
                ASTExprNodeStructLiteral::tValues values;
                values.push_back({{}, rcstringStart, mv$(node.left)});
                values.push_back({{}, rcstringEnd, mv$(node.right)});
                if (!newRange) {
                    values.push_back({{}, RcString::newInterned("exhausted"), makeNode<ASTExprNodeBool>(false)});
                }
                replacement = makeNode<ASTExprNodeStructLiteral>(mv$(pathRangeInclusiveNonEmpty), nullptr, mv$(values));
            } else {
                ASTExprNodeStructLiteral::tValues values;
                values.push_back({{}, rcstringEnd, mv$(node.right)});
                replacement = makeNode<ASTExprNodeStructLiteral>(mv$(pathRangeToInclusive), nullptr, mv$(values));
            }
            replacement->setSpan(node.span());
            break;
        }
        default:
            break;
    }
}

auto CExpandExpr::visit(ASTExprNodeUniOp& node) -> void {
    this->visitNodelete(node, node.value);
    if (node.type == ASTExprNodeUniOp::PinBorrow || node.type == ASTExprNodeUniOp::PinBorrowMut) {
        const bool isMut = node.type == ASTExprNodeUniOp::PinBorrowMut;
        auto pathNewUnchecked = getPath(crate.extCratenameCore, "pin", "Pin", "new_unchecked");
        auto borrow = makeNode<ASTExprNodeUniOp>(isMut ? ASTExprNodeUniOp::REFMUT : ASTExprNodeUniOp::REF, mv$(node.value));
        borrow->setSpan(node.span());
        auto call = makeNode<ASTExprNodeCallPath>(mv$(pathNewUnchecked), ::makeVec1(mv$(borrow)));
        call->setSpan(node.span());
        auto block = makeNode<ASTExprNodeBlock>();
        static_cast<ASTExprNodeBlock&>(*block).blockType = ASTExprNodeBlock::Type::Unsafe;
        static_cast<ASTExprNodeBlock&>(*block).nodes.push_back({false, mv$(call)});
        replacement = std::move(block);
        replacement->setSpan(node.span());
        return;
    }
    if (node.type == ASTExprNodeUniOp::QMARK) {
        if (expandState.mode != ExpandMode::Final) {
            return;
        }

        auto coreCrate = crate.extCratenameCore;

        // TODO: Find a way of creating bindings during HIR lower instead (so lang items are available)

        auto pathTry = getPath(coreCrate, "ops", "Try");
        const RcString rcstringV = RcString::newInterned("v");
        const RcString rcstringR = RcString::newInterned("r");
        {
            auto pathTryBranch = ASTPath::newUfcsTrait(::mkType(*parentExpandState.crate.pool, node.span()), pathTry, {ASTPathNode(RcString::newInterned("branch"))});
            auto path_ControlFlow_Continue = getPath(coreCrate, "ops", "ControlFlow", "Continue");
            auto path_ControlFlow_Break = getPath(coreCrate, "ops", "ControlFlow", "Break");
            auto pathFromResidualFromResidual = getPath(coreCrate, "ops", "FromResidual", "from_residual");

            std::vector<ASTExprNodeMatchArm> arms;
            arms.push_back(ASTExprNodeMatchArm(::makeVec1(ASTPattern(ASTPattern::TagNamedTuple(), node.span(), path_ControlFlow_Continue, ::makeVec1(ASTPattern(ASTPattern::TagBind(), node.span(), rcstringV)))), {}, makeNode<ASTExprNodeNamedValue>(ASTPath(rcstringV))));
            arms.push_back(ASTExprNodeMatchArm(::makeVec1(ASTPattern(ASTPattern::TagNamedTuple(), node.span(), path_ControlFlow_Break, ::makeVec1(ASTPattern(ASTPattern::TagBind(), node.span(), rcstringR)))), {}, makeNode<ASTExprNodeFlow>((tryStack.empty() ? ASTExprNodeFlow::RETURN : ASTExprNodeFlow::BREAK), (tryStack.empty() ? RcString("") : tryStack.back()), makeNode<ASTExprNodeCallPath>(ASTPath(pathFromResidualFromResidual), ::makeVec1(makeNode<ASTExprNodeNamedValue>(ASTPath(rcstringR)))))));

            replacement = makeNode<ASTExprNodeMatch>(makeNode<ASTExprNodeCallPath>(mv$(pathTryBranch), ::makeVec1(mv$(node.value))), mv$(arms));
        }
    }
}

auto CExpandExpr::visit(ASTExprNodeMacroDefinition&) -> void {
}
