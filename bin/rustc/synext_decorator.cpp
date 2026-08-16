#include "synext_decorator.h"

#include "common.h"
#include "synext.h"
#include "ast_ast.h"
#include "hir_hir.h" // ABI_RUST
#include "ast_expr.h"
#include "settings.h"
#include "ast_crate.h"
#include "expand_cfg.h"
#include "wire_board.h"
#include "ast_generics.h"
#include "parse_common.h"  // Parse_ModRoot_Items
#include "expand_common.h" // Expand_LookupMacro
#include "parse_ttstream.h"
#include "parse_parseerror.h" // ParseError
#include "expand_proc_macro.h"
#include "parse_interpolated_fragment.h"

namespace {
    class CommonFunction: public ExpandDecorator {
    public:
        virtual void handle(const ASTAttribute& mi, ASTFunction& fcn) const = 0;

        AttrStage stage() const override {
            return AttrStage::Pre;
        }

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
            if (i.is_None()) {
            } else if (i.is_Function()) {
                this->handle(mi, i.as_Function());
            } else {
                // TODO: Error
            }
        }

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
            if (i.is_None()) {
            } else if (i.is_Function()) {
                this->handle(mi, i.as_Function());
            } else {
                // TODO: Error
            }
        }

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const override {
            if (i.is_None()) {
            } else if (i.is_Function()) {
                this->handle(mi, i.as_Function());
            } else {
                // TODO: Error
            }
        }
    };
}

class CHandlerInline: public CommonFunction {
public:
    void handle(const ASTAttribute& mi, ASTFunction& fcn) const override {
        TTStream lex(mi.span(), ParseState(), mi.data());
        if (lex.getTokenIf(TOK_PAREN_OPEN)) {
            auto attr = lex.getTokenCheck(TOK_IDENT).ident().name;
            if (attr == "never") {
                fcn.markings.inlineType = ASTFunction::Markings::Inline::Never;
            } else if (attr == "always") {
                fcn.markings.inlineType = ASTFunction::Markings::Inline::Always;
            } else {
                ERROR(lex.pointSpan(), E0000, "Unknown inline type #[inline(" << attr << ")]");
            }
            lex.getTokenCheck(TOK_PAREN_CLOSE);
            lex.getTokenCheck(TOK_EOF);
        } else {
            fcn.markings.inlineType = ASTFunction::Markings::Inline::Normal;
        }
    }
};

STATIC_DECORATOR("inline", CHandlerInline);

class CHandlerCold: public CommonFunction {
public:
    void handle(const ASTAttribute& mi, ASTFunction& fcn) const override {
        TTStream lex(mi.span(), ParseState(), mi.data());
        lex.getTokenCheck(TOK_EOF);
        fcn.markings.isCold = true;
    }
};

STATIC_DECORATOR("cold", CHandlerCold);

class CHandlerRustcLegacyConstGenerics: public CommonFunction {
    void handle(const ASTAttribute& mi, ASTFunction& fcn) const override {
        TTStream lex(mi.span(), ParseState(), mi.data());
        lex.getTokenCheck(TOK_PAREN_OPEN);

        auto& list = fcn.markings.rustcLegacyConstGenerics;
        do {
            auto idxRaw = lex.getTokenCheck(TOK_INTEGER).intval();
            ASSERT_BUG(lex.pointSpan(), idxRaw < U128(UINT_MAX), "#[rustc_legacy_const_generics(" << idxRaw << ")] too large");
            auto idx = static_cast<unsigned>(idxRaw.truncateU64());
            ASSERT_BUG(lex.pointSpan(), std::find(list.begin(), list.end(), idx) == list.end(), "#[rustc_legacy_const_generics(" << idx << ")] duplicate index");
            list.push_back(idx);
        } while (lex.getTokenIf(TOK_COMMA));

        lex.getTokenCheck(TOK_PAREN_CLOSE);
        lex.getTokenCheck(TOK_EOF);
    }
};

STATIC_DECORATOR("rustc_legacy_const_generics", CHandlerRustcLegacyConstGenerics);

class CHandlerRepr: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        if (i.is_None()) {
        }
        // --- struct ---
        else if (auto* s = i.opt_Struct()) {
            TTStream lex(sp, ParseState(), mi.data());
            lex.parseState().wb = &wb;
            lex.getTokenCheck(TOK_PAREN_OPEN);
            // `#[repr()]` names no representation at all, which is allowed.
            if (lex.lookahead(0) == TOK_PAREN_CLOSE) {
                lex.getTokenCheck(TOK_PAREN_CLOSE);
                return;
            }
            do {
                auto reprType = lex.getTokenCheck(TOK_IDENT).ident().name;
                if (reprType == "C") {
                    switch (s->markings.repr) {
                        case ASTStruct::Markings::Repr::Rust:
                            s->markings.repr = ASTStruct::Markings::Repr::C;
                            break;
                        default:
                            // TODO: Error
                            break;
                    }
                } else if (reprType == "packed") {
                    switch (s->markings.repr) {
                        case ASTStruct::Markings::Repr::C:
                        case ASTStruct::Markings::Repr::Rust:
                            break;
                        default:
                            // TODO: Error
                            break;
                    }
                    if (s->markings.maxFieldAlign != 0) {
                        // TODO: Error
                    }
                    if (lex.getTokenIf(TOK_PAREN_OPEN)) {
                        auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
                        auto* val = cast<ASTExprNodeInteger>(&*n);
                        ASSERT_BUG(n->span(), val, "#[repr(packed(...))] - alignment must be an integer");
                        auto v = val->value;
                        ASSERT_BUG(lex.pointSpan(), v > U128(0), "#[repr(packed(" << v << "))] - alignment must be non-zero");
                        ASSERT_BUG(lex.pointSpan(), (v & (v - 1)) == U128(0), "#[repr(packed(" << v << "))] - alignment must be a power of two");
                        ASSERT_BUG(lex.pointSpan(), s->markings.alignValue == 0, "#[repr(packed(" << v << "))] - conflicts with previous alignment");
                        // TODO: I believe this should change the internal aligment too?
                        s->markings.maxFieldAlign = v.truncateU64();
                        lex.getTokenCheck(TOK_PAREN_CLOSE);
                    } else {
                        s->markings.maxFieldAlign = 1;
                    }
                } else if (reprType == "simd") {
                    s->markings.repr = ASTStruct::Markings::Repr::Simd;
                } else if (reprType == "transparent") {
                    s->markings.repr = ASTStruct::Markings::Repr::Transparent;
                } else if (reprType == "align") {
                    lex.getTokenCheck(TOK_PAREN_OPEN);
                    auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
                    auto* val = cast<ASTExprNodeInteger>(&*n);
                    ASSERT_BUG(n->span(), val, "#[repr(align(...))] - alignment must be an integer");
                    auto v = val->value;
                    ASSERT_BUG(lex.pointSpan(), v > U128(0), "#[repr(align(" << v << "))] - alignment must be non-zero");
                    ASSERT_BUG(lex.pointSpan(), (v & (v - 1)) == U128(0), "#[repr(align(" << v << "))] - alignment must be a power of two");
                    s->markings.alignValue = std::max(s->markings.alignValue, v.truncateU64());
                    lex.getTokenCheck(TOK_PAREN_CLOSE);
                } else if (reprType == "no_niche") {
                    // TODO: rust-lang/rust#68303 happens with UnsafeCell and niche optionisations
                    // - Would trustme also have this?
                } else {
                    TODO(sp, "Handle struct repr '" << reprType << "'");
                }
            } while (lex.getTokenIf(TOK_COMMA));
            lex.getTokenCheck(TOK_PAREN_CLOSE);
            lex.getTokenCheck(TOK_EOF);
        }
        // --- enum ---
        else if (auto* e = i.opt_Enum()) {
            TTStream lex(sp, ParseState(), mi.data());
            lex.parseState().wb = &wb;
            lex.getTokenCheck(TOK_PAREN_OPEN);

            // Loop, so `repr(C, u8)` is valid
            while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
                auto setRepr = [&](ASTEnum::Markings::Repr r) {
                    ASSERT_BUG(lex.pointSpan(), e->markings.repr == ASTEnum::Markings::Repr::Rust, "Multiple enum reprs set");
                    e->markings.repr = r;
                };
                auto reprStr = lex.getTokenCheck(TOK_IDENT).ident().name;
                if (reprStr == "C") {
                    // Repeated is OK
                    e->markings.isReprC = true;
                } else if (reprStr == "u8") {
                    setRepr(ASTEnum::Markings::Repr::U8);
                } else if (reprStr == "u16") {
                    setRepr(ASTEnum::Markings::Repr::U16);
                } else if (reprStr == "u32") {
                    setRepr(ASTEnum::Markings::Repr::U32);
                } else if (reprStr == "u64") {
                    setRepr(ASTEnum::Markings::Repr::U64);
                } else if (reprStr == "u128") {
                    setRepr(ASTEnum::Markings::Repr::U128);
                } else if (reprStr == "usize") {
                    setRepr(ASTEnum::Markings::Repr::Usize);
                } else if (reprStr == "i8") {
                    setRepr(ASTEnum::Markings::Repr::I8);
                } else if (reprStr == "i16") {
                    setRepr(ASTEnum::Markings::Repr::I16);
                } else if (reprStr == "i32") {
                    setRepr(ASTEnum::Markings::Repr::I32);
                } else if (reprStr == "i64") {
                    setRepr(ASTEnum::Markings::Repr::I64);
                } else if (reprStr == "i128") {
                    setRepr(ASTEnum::Markings::Repr::I128);
                } else if (reprStr == "isize") {
                    setRepr(ASTEnum::Markings::Repr::Isize);
                } else if (reprStr == "align") {
                    lex.getTokenCheck(TOK_PAREN_OPEN);
                    auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
                    auto* val = cast<ASTExprNodeInteger>(&*n);
                    ASSERT_BUG(n->span(), val, "#[repr(align(...))] - alignment must be an integer");
                    auto v = val->value;
                    ASSERT_BUG(lex.pointSpan(), v > U128(0), "#[repr(align(" << v << "))] - alignment must be non-zero");
                    ASSERT_BUG(lex.pointSpan(), (v & (v - 1)) == U128(0), "#[repr(align(" << v << "))] - alignment must be a power of two");
                    e->markings.alignValue = std::max(e->markings.alignValue, v.truncateU64());
                    lex.getTokenCheck(TOK_PAREN_CLOSE);
                } else {
                    ERROR(lex.pointSpan(), E0000, "Unknown enum repr '" << reprStr << "'");
                }
                if (!lex.getTokenIf(TOK_COMMA)) {
                    break;
                }
            }

            lex.getTokenCheck(TOK_PAREN_CLOSE);
            lex.getTokenCheck(TOK_EOF);
        }
        // --- union ---
        else if (auto* e = i.opt_Union()) {
            TTStream lex(sp, ParseState(), mi.data());
            lex.parseState().wb = &wb;
            lex.getTokenCheck(TOK_PAREN_OPEN);

            do {
                auto reprStr = lex.getTokenCheck(TOK_IDENT).ident().name;
                if (reprStr == "C") {
                    e->markings.repr = ASTUnion::Markings::Repr::C;
                } else if (reprStr == "transparent") {
                    e->markings.repr = ASTUnion::Markings::Repr::Transparent;
                } else if (reprStr == "packed") {
                    //switch( e->m_markings.repr )
                    //{
                    //case AST::Struct::Markings::Repr::C:
                    //case AST::Struct::Markings::Repr::Rust:
                    //default:
                    //    // TODO: Error
                    //}
                    //    // TODO: Error
                    //}
                    if (lex.getTokenIf(TOK_PAREN_OPEN)) {
                        auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
                        auto* val = cast<ASTExprNodeInteger>(&*n);
                        ASSERT_BUG(n->span(), val, "#[repr(packed(...))] - alignment must be an integer");
                        auto v = val->value;
                        ASSERT_BUG(lex.pointSpan(), v > U128(0), "#[repr(packed(" << v << "))] - alignment must be non-zero");
                        ASSERT_BUG(lex.pointSpan(), (v & (v - 1)) == U128(0), "#[repr(packed(" << v << "))] - alignment must be a power of two");
                        // TODO: I believe this should change the internal aligment too?
                        lex.getTokenCheck(TOK_PAREN_CLOSE);
                    } else {
                    }
                } else if (reprStr == "align") {
                    lex.getTokenCheck(TOK_PAREN_OPEN);
                    auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
                    auto* val = cast<ASTExprNodeInteger>(&*n);
                    ASSERT_BUG(n->span(), val, "#[repr(align(...))] - alignment must be an integer");
                    auto v = val->value;
                    ASSERT_BUG(lex.pointSpan(), v > U128(0), "#[repr(align(" << v << "))] - alignment must be non-zero");
                    ASSERT_BUG(lex.pointSpan(), (v & (v - 1)) == U128(0), "#[repr(align(" << v << "))] - alignment must be a power of two");
                    e->markings.alignValue = std::max(e->markings.alignValue, v.truncateU64());
                    lex.getTokenCheck(TOK_PAREN_CLOSE);
                } else {
                    ERROR(lex.pointSpan(), E0000, "Unknown union repr '" << reprStr << "'");
                }
            } while (lex.getTokenIf(TOK_COMMA));

            lex.getTokenCheck(TOK_PAREN_CLOSE);
            lex.getTokenCheck(TOK_EOF);
        } else {
            ERROR(mi.span(), E0000, "Unexpected attribute #[repr] on " << i.tagStr());
        }
    }
};

STATIC_DECORATOR("repr", CHandlerRepr);

class CHandlerRustcNonnullOptimizationGuaranteed: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        // TODO: Types only
        if (i.is_Struct()) {
        } else {
        }
    }
};

STATIC_DECORATOR("rustc_nonnull_optimization_guaranteed", CHandlerRustcNonnullOptimizationGuaranteed);

// 1.39
class CHandlerRustcLayoutScalarValidRangeStart: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        // TODO: Types only
        if (auto* s = i.opt_Struct()) {
            TTStream lex(sp, ParseState(), mi.data());
            lex.parseState().wb = &wb;
            lex.getTokenCheck(TOK_PAREN_OPEN);
            auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
            auto* np = cast<ASTExprNodeInteger>(n.get());
            ASSERT_BUG(n->span(), np, "#[rustc_layout_scalar_valid_range_start] requires an integer - got " << FMT_CB(ss, n->print(ss)));
            lex.getTokenCheck(TOK_PAREN_CLOSE);
            lex.getTokenCheck(TOK_EOF);

            s->markings.scalarValidStartSet = true;
            s->markings.scalarValidStart = np->value;
            DEBUG(path << " #[rustc_layout_scalar_valid_range_start]: " << std::hex << s->markings.scalarValidStart);
        } else {
            TODO(sp, "#[rustc_layout_scalar_valid_range_start] on " << i.tagStr());
        }
    }
};

STATIC_DECORATOR("rustc_layout_scalar_valid_range_start", CHandlerRustcLayoutScalarValidRangeStart);

class CHandlerRustcLayoutScalarValidRangeEnd: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        // TODO: Types only
        if (auto* s = i.opt_Struct()) {
            TTStream lex(sp, ParseState(), mi.data());
            lex.parseState().wb = &wb;
            lex.getTokenCheck(TOK_PAREN_OPEN);
            auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
            auto* np = cast<ASTExprNodeInteger>(n.get());
            ASSERT_BUG(n->span(), np, "#[rustc_layout_scalar_valid_range_end] requires an integer - got " << FMT_CB(ss, n->print(ss)));
            lex.getTokenCheck(TOK_PAREN_CLOSE);
            lex.getTokenCheck(TOK_EOF);
            s->markings.scalarValidEndSet = true;
            s->markings.scalarValidEnd = np->value;
            DEBUG(path << " #[rustc_layout_scalar_valid_range_end]: " << std::hex << s->markings.scalarValidEnd);
        } else {
            TODO(sp, "#[rustc_layout_scalar_valid_range_end] on " << i.tagStr());
        }
    }
};

STATIC_DECORATOR("rustc_layout_scalar_valid_range_end", CHandlerRustcLayoutScalarValidRangeEnd);

class CHandlerLinkName: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        auto linkName = mi.parseEqualsString(wb, crate, mod);
        ASSERT_BUG(sp, linkName != "", "Empty #[link_name] attribute");

        if (i.is_None()) {
        } else if (auto* fcn = i.opt_Function()) {
            ASSERT_BUG(sp, fcn->markings.linkName == "", "Duplicate #[link_name] attributes");
            fcn->markings.linkName = linkName;
        } else if (auto* st = i.opt_Static()) {
            ASSERT_BUG(sp, st->sClass() != ASTStatic::CONST, "#[link_name] on `const`");
            ASSERT_BUG(sp, st->markings.linkName == "", "Duplicate #[link_name] attributes");
            st->markings.linkName = linkName;
        } else {
        }
    }
};

STATIC_DECORATOR("link_name", CHandlerLinkName);

class CHandlerLinkSection: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        auto linkSection = mi.parseEqualsString(wb, crate, mod);
        ASSERT_BUG(sp, linkSection != "", "Empty #[link_section] attribute");

        if (i.is_None()) {
        } else if (auto* fcn = i.opt_Function()) {
            ASSERT_BUG(sp, fcn->markings.linkSection == "", "Duplicate #[link_section] attributes");
            fcn->markings.linkSection = linkSection;
        } else if (auto* st = i.opt_Static()) {
            ASSERT_BUG(sp, st->sClass() != ASTStatic::CONST, "#[link_section] on `const`");
            ASSERT_BUG(sp, st->markings.linkSection == "", "Duplicate #[link_section] attributes");
            st->markings.linkSection = linkSection;
        } else {
        }
    }
};

STATIC_DECORATOR("link_section", CHandlerLinkSection);

class CHandlerLink: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        if (i.is_None()) {
        } else if (auto* b = i.opt_ExternBlock()) {
            TTStream lex(sp, ParseState(), mi.data());
            lex.parseState().wb = &wb;
            lex.getTokenCheck(TOK_PAREN_OPEN);
            std::string libName;
            bool emit = true;
            ASTExternBlock::Link link;

            while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
                auto key = lex.getTokenCheck(TOK_IDENT).ident().name;
                if (key == "name") {
                    lex.getTokenCheck(TOK_EQUAL);
                    auto v = lex.getTokenCheck(TOK_STRING).str();
                    if (v == "") {
                        ERROR(sp, E0000, "Empty name on extern block");
                    }
                    link.libName = v;
                } else if (key == "kind") {
                    lex.getTokenCheck(TOK_EQUAL);
                    auto v = lex.getTokenCheck(TOK_STRING).str();
                    if (v == "") {
                        ERROR(sp, E0000, "Empty `kind` on extern block #[link]");
                    }
                    // TODO: save and use the kind
                } else if (key == "cfg") {
                    emit &= checkCfgStream(*wb.settings, lex);
                } else if (key == "modifiers") {
                    lex.getTokenCheck(TOK_EQUAL);
                    auto v = lex.getTokenCheck(TOK_STRING).str();
                    if (v == "") {
                        ERROR(sp, E0000, "Empty `modifiers` on extern block #[link]");
                    }
                    // TODO: save and use the `modifiers` value
                } else {
                    TODO(sp, "Unknown attribute `#[link(" << key << ")]`");
                }
                if (!lex.getTokenIf(TOK_COMMA)) {
                    break;
                }
            }
            if (link.libName == "") {
                ERROR(sp, E0000, "No name in `#[link]`");
            }
            if (emit) {
                b->libraries.push_back(std::move(link));
            }
            lex.getTokenCheck(TOK_PAREN_CLOSE);
            lex.getTokenCheck(TOK_EOF);
        } else {
            TODO(sp, "#[link] on " << i.tagStr());
        }
    }
};

STATIC_DECORATOR("link", CHandlerLink);

class CHandlerLinkage: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        TTStream lex(sp, ParseState(), mi.data());
        lex.parseState().wb = &wb;
        lex.getTokenCheck(TOK_EQUAL);
        auto tok = lex.getTokenCheck(TOK_STRING);
        auto linkageStr = tok.str();
        lex.getTokenCheck(TOK_EOF);

        auto linkage = ASTLinkage::Default;
        if (linkageStr == "extern_weak") {
            linkage = ASTLinkage::ExternWeak;
        } else if (linkageStr == "weak") {
            linkage = ASTLinkage::Weak;
        } else if (linkageStr == "external") {
        } else {
            TODO(sp, "#[linkage=\"" << linkageStr << "\"]");
        }

        if (auto* f = i.opt_Function()) {
            switch (linkage) {
                case ASTLinkage::Default:
                case ASTLinkage::Weak:
                    break;
                default:
                    TODO(sp, "#[linkage=\"" << linkageStr << "\"] on " << i.tagStr());
            }
            f->markings.linkage = linkage;
        } else if (auto* f = i.opt_Static()) {
            switch (linkage) {
                case ASTLinkage::Default:
                case ASTLinkage::Weak:
                case ASTLinkage::ExternWeak:
                    break;
                default:
                    TODO(sp, "#[linkage=\"" << linkageStr << "\"] on " << i.tagStr());
            }
            f->markings.linkage = linkage;
        } else {
            TODO(sp, "#[linkage] - " << i.tagStr() << " " << path << ": " << mi);
        }
    }
};

STATIC_DECORATOR("linkage", CHandlerLinkage);

class CHandlerTargetFeature: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        // TODO: Only valid on functions?
    }
};

STATIC_DECORATOR("target_feature", CHandlerTargetFeature);

class CHandlerRustcIntrinsic: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        if (auto* e = i.opt_Function()) {
            if (e->abi() != ABI_RUST) {
                ERROR(sp, E0000, "#[rustc_intrinsic] on function with ABI already set (`" << e->abi() << "`)");
            }
            // Only add if there's no body
            if (!e->code()) {
                e->setAbi("rust-intrinsic");
            }
        } else {
            ERROR(sp, E0000, "#[rustc_intrinsic] on non-function");
        }
    }
};

STATIC_DECORATOR("rustc_intrinsic", CHandlerRustcIntrinsic);

class CHandlerTrackCaller: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        if (/*auto* e =*/i.opt_Function()) {
            // Handled by HIR lower
        } else if (i.opt_Macro()) {
            // Accepted and ignored, matching rustc.
        } else if (const auto* invocation = i.opt_MacroInv(); invocation && invocation->path().isTrivial() && invocation->path().asTrivial() == "macro_rules") {
            // macro_rules! is still an invocation at the pre-expansion stage.
        } else {
            ERROR(sp, E0000, "#[track_caller] on non-function");
        }
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        if (/*auto* e =*/i.opt_Function()) {
            // Handled by HIR lower
        } else {
            ERROR(sp, E0000, "#[track_caller] on non-function");
        }
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const override {
        if (/*auto* e =*/i.opt_Function()) {
            // Handled by HIR lower
        } else {
            ERROR(sp, E0000, "#[track_caller] on non-function");
        }
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeP& expr) const override {
        if (auto* n = cast<ASTExprNodeClosure>(expr.get())) {
            n->trackCaller = true;
        } else {
            ERROR(sp, E0000, "#[track_caller] on non-function");
        }
    }
};

STATIC_DECORATOR("track_caller", CHandlerTrackCaller);

/// @brief Various unsafe attributes, addded around 1.90
class CHandlerUnsafe: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handleItem(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, const RcString& name, ASTItem& i) const {
        TTStream lex(mi.span(), ParseState(), mi.data());
        lex.parseState().wb = &wb;
        lex.getTokenCheck(TOK_PAREN_OPEN);
        while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
            auto ident = lex.getTokenCheck(TOK_IDENT).ident().name;

            if (ident == "no_mangle") {
                DEBUG("#[unsafe(no_mangle)] " << name);
                if (auto* e = i.opt_Function()) {
                    e->markings.linkName = name.c_str();
                } else if (auto* e = i.opt_Static()) {
                    e->markings.linkName = name.c_str();
                } else {
                    ERROR(sp, E0000, "#[unsafe(" << ident << ")] on bad item: " << i.tagStr());
                }
            } else if (ident == "export_name") {
                lex.getTokenCheck(TOK_EQUAL);
                auto s = lex.getTokenCheck(TOK_STRING).str();

                DEBUG("#[unsafe(export_name)] " << name << " as `" << s << "`");
                // The last name given wins, as it does in rustc: `#[unsafe(no_mangle)]`
                // alongside this one is a lint, not an error.
                if (auto* e = i.opt_Function()) {
                    e->markings.linkName = s;
                } else if (auto* e = i.opt_Static()) {
                    e->markings.linkName = s;
                } else {
                    ERROR(sp, E0000, "#[unsafe(" << ident << ")] on bad item: " << i.tagStr());
                }
            } else if (ident == "link_section") {
                lex.getTokenCheck(TOK_EQUAL);
                auto s = lex.getTokenCheck(TOK_STRING).str();

                DEBUG("#[unsafe(link_section)] " << name << " in `" << s);
                if (auto* e = i.opt_Function()) {
                    e->markings.linkSection = s;
                } else if (auto* e = i.opt_Static()) {
                    e->markings.linkSection = s;
                } else {
                    ERROR(sp, E0000, "#[unsafe(" << ident << ")] on bad item: " << i.tagStr());
                }
            } else if (ident == "ffi_const") {
                if (/*auto* e =*/i.opt_Function()) {
                    // A hint to the optimiser that a FFI function always returns the same value
                    // - Don't care here
                } else {
                    ERROR(sp, E0000, "#[unsafe(" << ident << ")] on non-function");
                }
            } else if (ident == "naked") {
                if (auto* e = i.opt_Function()) {
                    // Flag this function as being a bare symbol (no prologue/epilogue)
                    e->markings.isNaked = true;
                } else {
                    ERROR(sp, E0000, "#[unsafe(" << ident << ")] on non-function");
                }
            } else {
                ERROR(sp, E0000, "Unknown #[unsafe(" << ident << ")]");
            }

            if (lex.lookahead(0) != TOK_COMMA) {
                break;
            }
            lex.getTokenCheck(TOK_COMMA);
        }
        lex.getTokenCheck(TOK_PAREN_CLOSE);
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate&, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute>, const ASTVisibility&, ASTItem& i) const override {
        handleItem(sp, mi, wb, path.nodes.back(), i);
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate&, ASTImpl&, const RcString& name, slice<const ASTAttribute>, const ASTVisibility&, ASTItem& i) const override {
        handleItem(sp, mi, wb, name, i);
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate&, const ASTAbsolutePath& path, ASTTrait&, slice<const ASTAttribute>, ASTItem& i) const override {
        handleItem(sp, mi, wb, path.nodes.back(), i);
    }
};

STATIC_DECORATOR("unsafe", CHandlerUnsafe);

class DecoratorCrateType: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override {
        auto name = mi.parseEqualsString(wb, crate, crate.rootModule_);
        if (name == "rlib" || name == "lib") {
            crate.crateType = ASTCrate::Type::RustLib;
        } else if (name == "dylib" || name == "rdylib") {
            crate.crateType = ASTCrate::Type::RustDylib;
        } else if (name == "cdylib") {
            crate.crateType = ASTCrate::Type::CDylib;
        } else if (name == "proc-macro") {
            crate.crateType = ASTCrate::Type::ProcMacro;
        } else {
            ERROR(sp, E0000, "Unknown crate type '" << name << "'");
        }
    }
};

class DecoratorCrateName: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override {
        auto name = mi.parseEqualsString(wb, crate, crate.rootModule_);
        crate.setCrateName(name);
    }
};

class DecoratorFeature: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override {
        mi.parseParenIdentList([&](const Span&, RcString feature) {
            crate.features.insert(feature);
        });
    }
};
STATIC_DECORATOR("feature", DecoratorFeature)

class DecoratorAllocator: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override {
        // TODO: Check for an existing allocator crate
        crate.langItems.insert(::std::make_pair("trustme-allocator", ASTAbsolutePath()));
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        if (!i.is_Function()) {
            ERROR(sp, E0000, "#[allocator] can only be put on functions and the crate - found on " << i.tagStr());
        }
        // TODO: Ensure that this is an extern { fn }
        // TODO: Does this need to do anything?
    }
};

class DecoratorPanicRuntime: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override {
        // TODO: Check for an existing panic_runtime crate
        crate.langItems.insert(::std::make_pair("trustme-panic_runtime", ASTAbsolutePath()));
    }
};

class DecoratorNeedsPanicRuntime: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override {
        crate.langItems.insert(::std::make_pair("trustme-needs_panic_runtime", ASTAbsolutePath()));
    }
};

STATIC_DECORATOR("crate_type", DecoratorCrateType)
STATIC_DECORATOR("crate_name", DecoratorCrateName)

STATIC_DECORATOR("allocator", DecoratorAllocator)
STATIC_DECORATOR("panic_runtime", DecoratorPanicRuntime)
STATIC_DECORATOR("needs_panic_runtime", DecoratorNeedsPanicRuntime)

namespace {
    const RcString rcstringSelf = RcString::newInterned("Self");
    const RcString rcstringH = RcString::newInterned("H");
    const RcString rcstringSelfLower = RcString::newInterned("self");
    const RcString rcstringV = RcString::newInterned("v");
    const RcString rcstringS = RcString::newInterned("s");
    const RcString rcstringFmt = RcString::newInterned("fmt");

    const RcString rcstringRes = RcString::newInterned("res");

    const RcString rcstringF = RcString::newInterned("f");
    const RcString rcstringField = RcString::newInterned("field");

    const RcString rcstringWriteStr = RcString::newInterned("write_str");
    const RcString rcstringFinish = RcString::newInterned("finish");

    const RcString rcstringClone = RcString::newInterned("Clone");
    const RcString rcstringCloneLower = RcString::newInterned("clone");

    const RcString rcstringState = RcString::newInterned("state");

    const RcString rcstringAssertReceiverIsTotalEq = RcString::newInterned("assert_receiver_is_total_eq");

    ASTType* mktypeSelf(stl::ObjPool& pool, const Span& sp) {
        return mkType(pool, sp, rcstringSelf, 0xFFFF);
    }
}

template <typename T>
static inline ::std::vector<T> vec$(T v1) {
    ::std::vector<T> tmp;
    tmp.push_back(mv$(v1));
    return mv$(tmp);
}

template <typename T>
static inline ::std::vector<T> vec$(T v1, T v2) {
    ::std::vector<T> tmp;
    tmp.reserve(2);
    tmp.push_back(mv$(v1));
    tmp.push_back(mv$(v2));
    return mv$(tmp);
}

template <typename T>
static inline ::std::vector<T> vec$(T v1, T v2, T v3) {
    ::std::vector<T> tmp;
    tmp.reserve(3);
    tmp.push_back(mv$(v1));
    tmp.push_back(mv$(v2));
    tmp.push_back(mv$(v3));
    return mv$(tmp);
}

template <typename T>
static inline ::std::vector<T> vec$(T v1, T v2, T v3, T v4) {
    ::std::vector<T> tmp;
    tmp.reserve(4);
    tmp.push_back(mv$(v1));
    tmp.push_back(mv$(v2));
    tmp.push_back(mv$(v3));
    tmp.push_back(mv$(v4));
    return mv$(tmp);
}

template <typename T>
static inline ::std::vector<T> vec$(T v1, T v2, T v3, T v4, T v5) {
    ::std::vector<T> tmp;
    tmp.reserve(5);
    tmp.push_back(mv$(v1));
    tmp.push_back(mv$(v2));
    tmp.push_back(mv$(v3));
    tmp.push_back(mv$(v4));
    tmp.push_back(mv$(v5));
    return mv$(tmp);
}

static ASTPath getPath(const RcString& coreName, const char* c1, const char* c2) {
    return ASTAbsolutePath(coreName, {RcString::newInterned(c1), RcString::newInterned(c2)});
}

static ASTPath getPath(const RcString& coreName, const char* c1, const char* c2, const char* c3) {
    return ASTAbsolutePath(coreName, {RcString::newInterned(c1), RcString::newInterned(c2), RcString::newInterned(c3)});
}

static std::unique_ptr<ASTExprNodeBlock> newBlock(const Span& sp) {
    auto rv = ::std::make_unique<ASTExprNodeBlock>();
    rv->setSpan(sp);
    return rv;
}

static inline ASTExprNodeP mkExprnodep(ASTExprNode* en) {
    return ASTExprNodeP(en);
}

//#define NEWNODE(type, ...)  mk_exprnodep(new type(__VA_ARGS__))
#define NEWNODE(type, ...) mkExprnodep(new ASTExprNode##type(__VA_ARGS__))

static void makeRefpatA(const Span& sp, ASTExprNodeBlock& block, ::std::vector<ASTPattern>& patsA, const ::std::vector<ASTTupleItem>& subTypes, ::std::function<ASTExprNodeP(size_t, ASTExprNodeP)> cb) {
    ::std::vector<ASTExprNodeBlock::Line> nodes;
    for (size_t idx = 0; idx < subTypes.size(); idx++) {
        auto nameA = RcString::newInterned(FMT("a" << idx));
        patsA.push_back(ASTPattern(ASTPattern::TagBind(), sp, nameA, ASTPatternBinding::Type::REF));
        block.pushStmt(cb(idx, NEWNODE(NamedValue, ASTPath(nameA))));
    }
}

static void makeRefpatA(const Span& sp, ASTExprNodeBlock& block, ::std::vector<ASTStructPatternEntry>& patsA, const ::std::vector<ASTStructItem>& fields, ::std::function<ASTExprNodeP(size_t, ASTExprNodeP)> cb) {
    ::std::vector<ASTExprNodeBlock::Line> nodes;
    size_t idx = 0;
    for (const auto& fld : fields) {
        auto nameA = RcString::newInterned(FMT("a" << fld.name));
        patsA.push_back(ASTStructPatternEntry{ASTAttributeList(), fld.name, ASTPattern(ASTPattern::TagBind(), sp, nameA, ASTPatternBinding::Type::REF)});
        block.pushStmt(cb(idx, NEWNODE(NamedValue, ASTPath(nameA))));
        idx++;
    }
}

static void makeRefpatAb(const Span& sp, ASTExprNodeBlock& block, ::std::vector<ASTPattern>& patsA, ::std::vector<ASTPattern>& patsB, const ::std::vector<ASTTupleItem>& subTypes, ::std::function<ASTExprNodeP(size_t, ASTExprNodeP, ASTExprNodeP)> cb) {
    for (size_t idx = 0; idx < subTypes.size(); idx++) {
        auto nameA = RcString::newInterned(FMT("a" << idx));
        auto nameB = RcString::newInterned(FMT("b" << idx));
        patsA.push_back(ASTPattern(ASTPattern::TagBind(), sp, nameA, ASTPatternBinding::Type::REF));
        patsB.push_back(ASTPattern(ASTPattern::TagBind(), sp, nameB, ASTPatternBinding::Type::REF));
        block.pushStmt(cb(idx, NEWNODE(NamedValue, ASTPath(nameA)), NEWNODE(NamedValue, ASTPath(nameB))));
    }
}

static void makeRefpatAb(const Span& sp, ASTExprNodeBlock& block, ::std::vector<ASTStructPatternEntry>& patsA, ::std::vector<ASTStructPatternEntry>& patsB, const ::std::vector<ASTStructItem>& fields, ::std::function<ASTExprNodeP(size_t, ASTExprNodeP, ASTExprNodeP)> cb) {
    size_t idx = 0;
    for (const auto& fld : fields) {
        auto nameA = RcString::newInterned(FMT("a" << fld.name));
        auto nameB = RcString::newInterned(FMT("b" << fld.name));
        patsA.push_back(ASTStructPatternEntry{ASTAttributeList(), fld.name, ASTPattern(ASTPattern::TagBind(), sp, nameA, ASTPatternBinding::Type::REF)});
        patsB.push_back(ASTStructPatternEntry{ASTAttributeList(), fld.name, ASTPattern(ASTPattern::TagBind(), sp, nameB, ASTPatternBinding::Type::REF)});
        block.pushStmt(cb(idx, NEWNODE(NamedValue, ASTPath(nameA)), NEWNODE(NamedValue, ASTPath(nameB))));
        idx++;
    }
}

struct DeriveOpts {
    RcString coreName;
    /// The item also derives `Copy`, which makes a derived `Clone` a copy
    /// rather than a field-by-field clone.
    bool derivesCopy = false;
};

/// Interface for derive handlers
struct Deriver {
    virtual ~Deriver() = default;
    virtual const char* traitName() const = 0;
    virtual ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const = 0;
    virtual ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const = 0;

    virtual ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTUnion& unn) const {
        ERROR(sp, E0000, "Cannot derive(" << traitName() << ") on union");
    }

    void iterateStructFields(const ASTStruct& str, ::std::function<void(RcString)> cb) const {
        TU_MATCH_HDRA((str.data), {)
        TU_ARMA(Unit, e) {
            }
            TU_ARMA(Struct, e) {
                for (const auto& fld : e.ents) {
                    cb(fld.name);
                }
            }
            TU_ARMA(Tuple, e) {
                for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                    auto fldName = RcString::newInterned(FMT(idx));
                    cb(fldName);
                }
            }
        }
    }

    ASTGenericParams getParamsWithBounds(stl::ObjPool& pool, const Span& sp, const ASTGenericParams& p, const ASTPath& traitPath, ::std::vector<ASTType*> additionalBoundedTypes, bool boundTypeParams = true) const {
        ASTGenericParams params = p.clone();

        // TODO: Get bounds based on generic (or similar) types used within the type.
        // - How would this code (that runs before resolve) know what's a generic and what's a local type?
        // - Searches within the type for a Path that starts with that param.

        unsigned int i = 0;
        for (const auto& arg : params.params) {
            if (const auto* e = arg.opt_Type()) {
                if (boundTypeParams) {
                    params.addBound(ASTGenericBound::make_IsTrait({sp, {}, mkType(pool, sp, e->name(), i), {}, traitPath}));
                }
                i++;
            }
        }

        // For each field type
        // - Locate used generic parameters in the type (and sub-types that directly use said parameter)
        for (auto& ty : additionalBoundedTypes) {
            params.addBound(ASTGenericBound::make_IsTrait({sp, {}, mv$(ty), {}, traitPath}));
        }

        return params;
    }

    ::std::vector<ASTType*> getFieldBounds(const ASTStruct& str) const {
        ::std::vector<ASTType*> ret;
        TU_MATCH(ASTStructData, (str.data), (e), (Unit, ), (Struct, for (const auto& fld : e.ents) { addFieldBoundFromTy(str.params(), ret, fld.type); }), (Tuple, for (const auto& ent : e.ents) { addFieldBoundFromTy(str.params(), ret, ent.type); }))
        return ret;
    }

    ::std::vector<ASTType*> getFieldBounds(const ASTEnum& enm) const {
        ::std::vector<ASTType*> ret;

        for (const auto& v : enm.variants()) {
            TU_MATCH(ASTEnumVariantData, (v.data), (e), (Unit, ), (Tuple, for (const auto& ent : e.items) { addFieldBoundFromTy(enm.params(), ret, ent.type); }), (Struct, for (const auto& fld : e.fields) { addFieldBoundFromTy(enm.params(), ret, fld.type); }))
        }

        return ret;
    }

    ::std::vector<ASTType*> getFieldBounds(const ASTUnion& unn) const {
        ::std::vector<ASTType*> ret;
        for (const auto& fld : unn.variants) {
            addFieldBoundFromTy(unn.params(), ret, fld.type);
        }
        return ret;
    }

    void addFieldBoundFromTy(const ASTGenericParams& params, ::std::vector<ASTType*>& outList, ASTType* ty) const {
        struct H {
            static void visitNodes(const Deriver& self, const ASTGenericParams& params, ::std::vector<ASTType*>& outList, const ::std::vector<ASTPathNode>& nodes) {
                for (const auto& node : nodes) {
                    for (const auto& e : node.args().entries) {
                        TU_MATCH_HDRA( (e), {)
                        default:
                            break;
                            TU_ARMA(Type, ty) {
                                self.addFieldBoundFromTy(params, outList, ty);
                            }
                            TU_ARMA(AssociatedTyEqual, aty) {
                                self.addFieldBoundFromTy(params, outList, aty.second);
                            }
                        }
                    }
                }
            }
        };

        // TODO: Locate type that is directly related to the type param.
        TU_MATCH_HDRA( (ty->data), {)
        TU_ARMA(None, e) {
                // Wat?
            }
            TU_ARMA(Any, e) {
                // Nope.
            }
            TU_ARMA(Unit, e) {
            }
            TU_ARMA(Bang, e) {
            }
            TU_ARMA(Macro, e) {
                // not allowed
            }
            TU_ARMA(Primitive, e) {
            }
            TU_ARMA(Function, e) {
                // TODO? Well... function types don't tend to depend on the trait?
            }
            TU_ARMA(Tuple, e) {
                for (const auto& sty : e.innerTypes) {
                    addFieldBoundFromTy(params, outList, sty);
                }
            }
            TU_ARMA(Borrow, e) {
                addFieldBoundFromTy(params, outList, e.inner);
            }
            TU_ARMA(Pointer, e) {
                addFieldBoundFromTy(params, outList, e.inner);
            }
            TU_ARMA(Array, e) {
                addFieldBoundFromTy(params, outList, e.inner);
            }
            TU_ARMA(Slice, e) {
                addFieldBoundFromTy(params, outList, e.inner);
            }
            TU_ARMA(Generic, e) {
                // Although this is what we're looking for, it's already handled.
            }
            TU_ARMA(Path, e) {
            TU_MATCH_HDRA( (e->cls), {)
            TU_ARMA(Invalid, pe) {
                        // wut.
                    }
                    TU_ARMA(Local, pe) {
                    }
                    TU_ARMA(Relative, pe) {
                        if (pe.nodes.size() > 1) {
                            // Check if the first node of a relative is a generic param.
                            for (const auto& param : params.params) {
                                if (TU_TEST1(param, Type, .name() == pe.nodes.front().name())) {
                                    addFieldBound(outList, ty);
                                    break;
                                }
                            }
                        }
                        H::visitNodes(*this, params, outList, pe.nodes);
                    }
                    TU_ARMA(Self, pe) {
                    }
                    TU_ARMA(Super, pe) {
                    }
                    TU_ARMA(Absolute, pe) {
                    }
                    TU_ARMA(UFCS, pe) {
                    }
            }
            }
            TU_ARMA(TraitObject, e) {
                // TODO: Should this be recursed?
            }
            TU_ARMA(ErasedType, e) {
                // TODO: Should this be recursed?
            }
        }
    }

    void addFieldBound(::std::vector<ASTType*>& outList, ASTType* type) const {
        for (const auto& ty : outList) {
            if (ty == type) {
                return;
            }
        }

        outList.push_back(type->clone());
    }
};

/// 'Debug' derive handler
class DeriverDebug: public Deriver {
    ASTImpl makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, ::std::vector<ASTType*> typesToBound, ASTExprNodeP node) const {
        const ASTPath debugTrait = getPath(coreName, "fmt", "Debug");

        ASTFunction fcn(sp, mkType(*type->pool, sp, getPath(coreName, "fmt", "Result")), vec$(ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, rcstringSelfLower), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), false, mktypeSelf(*type->pool, sp))), ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, rcstringF), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), true, mkType(*type->pool, sp, getPath(coreName, "fmt", "Formatter"))))));
        fcn.setCode(NEWNODE(Block, mv$(node)));

        ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, debugTrait, mv$(typesToBound));

        ASTImpl rv(ASTImplDef(mv$(params), makeSpanned(sp, debugTrait), type->clone()));
        rv.addFunction(sp, {}, ASTVisibility::makeBarePrivate(), false, rcstringFmt, mv$(fcn));
        return mv$(rv);
    }

public:
    const char* traitName() const override {
        return "Debug";
    }

    // The derived code calls the formatter helpers by path rather than as
    // methods. A method call is resolved against whatever is in scope, so a
    // user trait with a `field`, `finish` or `debug_struct` method on a blanket
    // impl would capture the call.
    static ASTExprNodeP callPath(ASTPath path, const char* method, ::std::vector<ASTExprNodeP> args) {
        return NEWNODE(CallPath, path + RcString::newInterned(method), mv$(args));
    }

    static ASTExprNodeP builderRef() {
        return NEWNODE(UniOp, ASTExprNodeUniOp::REFMUT, NEWNODE(NamedValue, ASTPath(RcString::newInterned("s"))));
    }

    static ASTPattern builderPattern(const Span& sp) {
        return ASTPattern(ASTPattern::TagBind(), sp, RcString::newInterned("s"), ASTPatternBinding::Type::MOVE, /*isMut=*/true);
    }

    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const override {
        ::std::string name = type->path().nodes().back().name().c_str();
        const ASTPath pathFormatter = getPath(opts.coreName, "fmt", "Formatter");
        const ASTPath pathDebugStruct = getPath(opts.coreName, "fmt", "DebugStruct");
        const ASTPath pathDebugTuple = getPath(opts.coreName, "fmt", "DebugTuple");

        // Generate code for Debug
        ASTExprNodeP node;
        TU_MATCH_HDRA((str.data), {)
        TU_ARMA(Unit, e) {
                node = callPath(pathFormatter, "write_str", vec$(NEWNODE(NamedValue, ASTPath(rcstringF)), NEWNODE(String, name)));
            }
            TU_ARMA(Struct, e) {
                std::vector<ASTExprNodeBlock::Line> nodes;
                nodes.push_back({true, NEWNODE(LetBinding, builderPattern(sp), mkType(*type->pool, sp), callPath(pathFormatter, "debug_struct", vec$(NEWNODE(NamedValue, ASTPath(rcstringF)), NEWNODE(String, name))))});
                for (const auto& fld : e.ents) {
                    nodes.push_back({true, callPath(pathDebugStruct, "field", vec$(builderRef(), NEWNODE(String, fld.name.c_str()), NEWNODE(UniOp, ASTExprNodeUniOp::REF, NEWNODE(UniOp, ASTExprNodeUniOp::REF, NEWNODE(Field, NEWNODE(NamedValue, ASTPath(rcstringSelfLower)), fld.name)))))});
                }
                nodes.push_back({false, callPath(pathDebugStruct, "finish", vec$(builderRef()))});
                node = NEWNODE(Block, mv$(nodes));
            }
            TU_ARMA(Tuple, e) {
                std::vector<ASTExprNodeBlock::Line> nodes;
                nodes.push_back({true, NEWNODE(LetBinding, builderPattern(sp), mkType(*type->pool, sp), callPath(pathFormatter, "debug_tuple", vec$(NEWNODE(NamedValue, ASTPath(rcstringF)), NEWNODE(String, name))))});
                for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                    nodes.push_back({true, callPath(pathDebugTuple, "field", vec$(builderRef(), NEWNODE(UniOp, ASTExprNodeUniOp::REF, NEWNODE(UniOp, ASTExprNodeUniOp::REF, NEWNODE(Field, NEWNODE(NamedValue, ASTPath(rcstringSelfLower)), RcString::newInterned(FMT(idx)))))))});
                }
                nodes.push_back({false, callPath(pathDebugTuple, "finish", vec$(builderRef()))});
                node = NEWNODE(Block, mv$(nodes));
            }
        }

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), mv$(node));
    }

    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const override {
        ASTPath basePath = *type->data.as_Path();
        basePath.nodes().back() = basePath.nodes().back().name();
        const ASTPath pathFormatter = getPath(opts.coreName, "fmt", "Formatter");
        const ASTPath pathDebugStruct = getPath(opts.coreName, "fmt", "DebugStruct");
        const ASTPath pathDebugTuple = getPath(opts.coreName, "fmt", "DebugTuple");

        ::std::vector<ASTExprNodeMatchArm> arms;
        for (const auto& v : enm.variants()) {
            ASTExprNodeP code;
            ASTPattern patA;

            ASTPath variantPath = basePath + v.name;

            TU_MATCH_HDRA( (v.data), {)
            TU_ARMA(Unit, e) {
                    code = callPath(pathFormatter, "write_str", vec$(NEWNODE(NamedValue, ASTPath(rcstringF)), NEWNODE(String, v.name.c_str())));
                    patA = ASTPattern(ASTPattern::TagValue(), sp, ASTPattern::Value::make_Named(variantPath));
                }
                TU_ARMA(Tuple, e) {
                    ::std::vector<ASTPattern> patsA;
                    auto block = newBlock(sp);
                    block->pushStmt(NEWNODE(LetBinding, builderPattern(sp), mkType(*type->pool, sp), callPath(pathFormatter, "debug_tuple", vec$(NEWNODE(NamedValue, ASTPath(rcstringF)), NEWNODE(String, v.name.c_str())))));

                    makeRefpatA(sp, *block, patsA, e.items, [&](size_t idx, auto a) {
                        return callPath(pathDebugTuple, "field", vec$(builderRef(), mv$(a)));
                    });
                    block->pushTailExpr(callPath(pathDebugTuple, "finish", vec$(builderRef())));
                    code = mkExprnodep(block.release());
                    patA = ASTPattern(ASTPattern::TagNamedTuple(), sp, variantPath, mv$(patsA));
                }
                TU_ARMA(Struct, e) {
                    ::std::vector<ASTStructPatternEntry> patsA;
                    auto block = newBlock(sp);
                    block->pushStmt(NEWNODE(LetBinding, builderPattern(sp), mkType(*type->pool, sp), callPath(pathFormatter, "debug_struct", vec$(NEWNODE(NamedValue, ASTPath(rcstringF)), NEWNODE(String, v.name.c_str())))));

                    makeRefpatA(sp, *block, patsA, e.fields, [&](size_t idx, auto a) {
                        return callPath(pathDebugStruct, "field", vec$(builderRef(), NEWNODE(String, e.fields[idx].name.c_str()), mv$(a)));
                    });
                    block->pushTailExpr(callPath(pathDebugStruct, "finish", vec$(builderRef())));

                    code = mkExprnodep(block.release());
                    patA = ASTPattern(ASTPattern::TagStruct(), sp, variantPath, mv$(patsA), true);
                }
            }

            ::std::vector< ASTPattern>    pats;
            pats.push_back( ASTPattern(ASTPattern::TagReference(), sp, false, mv$(patA)) );

            arms.push_back(ASTExprNodeMatchArm(
                mv$(pats),
                {},
                mv$(code)
                ));
        }
        ASTExprNodeP node = NEWNODE(Match, NEWNODE(NamedValue, ASTPath(rcstringSelfLower)), mv$(arms));

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), mv$(node));
    }
} gDeriveDebug;

// ---- Comparisons

class DeriverInnerCompare: public Deriver {
protected:
    /// Create a final output impl block
    virtual ASTImpl makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, ::std::vector<ASTType*> typesToBound, ASTExprNodeP node) const = 0;
    /// Compare two values, early returning if no more comparisons should happen
    virtual ASTExprNodeP compareAndRet(Span sp, const RcString& coreName, ASTExprNodeP v1, ASTExprNodeP v2) const = 0;
    /// Get the return value for if `compare_and_ret` didn't return early
    virtual ASTExprNodeP equalValue(Span sp, const RcString& coreName) const = 0;
    /// Get the return value for a mismatch in enum variants
    virtual ASTExprNodeP enumMismatch(Span sp, const RcString& coreName) const = 0;

public:
    // Struct
    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const override {
        auto block = newBlock(sp);

        this->iterateStructFields(str, [&](RcString fldName) {
            block->pushStmt(this->compareAndRet(sp, opts.coreName, NEWNODE(Field, NEWNODE(NamedValue, ASTPath(rcstringSelfLower)), fldName), NEWNODE(Field, NEWNODE(NamedValue, ASTPath(rcstringV)), fldName)));
        });
        block->pushTailExpr(this->equalValue(sp, opts.coreName));

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), mkExprnodep(block.release()));
    }

    // Enum
    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const override {
        ASTPath basePath = *type->data.as_Path();
        basePath.nodes().back().args() = ASTPathParams();
        ::std::vector<ASTExprNodeMatchArm> arms;

        for (const auto& v : enm.variants()) {
            ASTExprNodeP code;
            ASTPattern patA;
            ASTPattern patB;
            auto variantPath = basePath + v.name;

            TU_MATCH_HDRA( (v.data), {)
            TU_ARMA(Unit, e) {
                    code = this->equalValue(sp, opts.coreName);
                    patA = ASTPattern(ASTPattern::TagValue(), sp, ASTPattern::Value::make_Named(variantPath));
                    patB = ASTPattern(ASTPattern::TagValue(), sp, ASTPattern::Value::make_Named(variantPath));
                }
                TU_ARMA(Tuple, e) {
                    auto block = newBlock(sp);
                    ::std::vector<ASTPattern> patsA;
                    ::std::vector<ASTPattern> patsB;

                    makeRefpatAb(sp, *block, patsA, patsB, e.items, [&](auto idx, auto a, auto b) {
                        return this->compareAndRet(sp, opts.coreName, mv$(a), mv$(b));
                    });
                    block->pushTailExpr(this->equalValue(sp, opts.coreName));

                    patA = ASTPattern(ASTPattern::TagNamedTuple(), sp, variantPath, mv$(patsA));
                    patB = ASTPattern(ASTPattern::TagNamedTuple(), sp, variantPath, mv$(patsB));
                    code = mkExprnodep(block.release());
                }
                TU_ARMA(Struct, e) {
                    auto block = newBlock(sp);
                    ::std::vector<ASTStructPatternEntry> patsA;
                    ::std::vector<ASTStructPatternEntry> patsB;

                    makeRefpatAb(sp, *block, patsA, patsB, e.fields, [&](const auto& name, auto a, auto b) {
                        return this->compareAndRet(sp, opts.coreName, mv$(a), mv$(b));
                    });
                    block->pushTailExpr(this->equalValue(sp, opts.coreName));

                    patA = ASTPattern(ASTPattern::TagStruct(), sp, variantPath, mv$(patsA), true);
                    patB = ASTPattern(ASTPattern::TagStruct(), sp, variantPath, mv$(patsB), true);
                    code = mkExprnodep(block.release());
                }
            }

            ::std::vector< ASTPattern>    pats;
            {
                ::std::vector<ASTPattern> tuplePats;
                tuplePats.push_back(ASTPattern(ASTPattern::TagReference(), sp, false, mv$(patA)));
                tuplePats.push_back(ASTPattern(ASTPattern::TagReference(), sp, false, mv$(patB)));
                pats.push_back(ASTPattern(ASTPattern::TagTuple(), sp, mv$(tuplePats)));
            }

            arms.push_back(ASTExprNodeMatchArm(
                mv$(pats),
                {},
                mv$(code)
                ));
        }

        // Default arm
        {
            arms.push_back(ASTExprNodeMatchArm(::makeVec1(ASTPattern()), {}, this->enumMismatch(sp, opts.coreName)));
        }

        ::std::vector<ASTExprNodeP> vals;
        vals.push_back(NEWNODE(NamedValue, ASTPath(rcstringSelfLower)));
        vals.push_back(NEWNODE(NamedValue, ASTPath(rcstringV)));
        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), NEWNODE(Match, NEWNODE(Tuple, mv$(vals)), mv$(arms)));
    }
};

class DeriverPartialEq: public DeriverInnerCompare {
    ASTImpl makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, ::std::vector<ASTType*> typesToBound, ASTExprNodeP node) const override {
        const ASTPath traitPath = getPath(coreName, "cmp", "PartialEq");

        ASTFunction fcn(sp, mkType(*type->pool, sp, CORETYPE_BOOL), vec$(ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, rcstringSelfLower), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), false, mktypeSelf(*type->pool, sp))), ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, rcstringV), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), false, mktypeSelf(*type->pool, sp)))));
        fcn.setCode(NEWNODE(Block, mv$(node)));

        ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, traitPath, mv$(typesToBound));

        ASTImpl rv(ASTImplDef(mv$(params), makeSpanned(sp, traitPath), type->clone()));
        rv.addFunction(sp, {}, ASTVisibility::makeBarePrivate(), false, RcString::newInterned("eq"), mv$(fcn));
        return mv$(rv);
    }

    ASTExprNodeP compareAndRet(Span sp, const RcString& coreName, ASTExprNodeP v1, ASTExprNodeP v2) const override {
        std::vector<ASTExprNodeIf::Arm> arms;
        arms.push_back(ASTExprNodeIf::Arm{makeVec1(ASTIfLetCondition{{}, NEWNODE(BinOp, ASTExprNodeBinOp::CMPNEQU, mv$(v1), mv$(v2))}), NEWNODE(Flow, ASTExprNodeFlow::RETURN, "", NEWNODE(Bool, false))});
        return NEWNODE(If, std::move(arms), nullptr);
    }

    ASTExprNodeP equalValue(Span sp, const RcString& coreName) const override {
        return NEWNODE(Bool, true);
    }

    ASTExprNodeP enumMismatch(Span sp, const RcString& coreName) const override {
        return NEWNODE(Bool, false);
    }

public:
    const char* traitName() const override {
        return "PartialEq";
    }
} gDerivePartialeq;

class DeriverPartialOrd: public DeriverInnerCompare {
    ASTImpl makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, ::std::vector<ASTType*> typesToBound, ASTExprNodeP node) const override {
        const ASTPath traitPath = getPath(coreName, "cmp", "PartialOrd");
        const ASTPath pathOrdering = getPath(coreName, "cmp", "Ordering");

        ASTPath pathOptionOrdering = getPath(coreName, "option", "Option");
        pathOptionOrdering.nodes().back().args().entries.push_back(mkType(*type->pool, sp, pathOrdering));

        ASTFunction fcn(sp, mkType(*type->pool, sp, pathOptionOrdering), vec$(ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, rcstringSelfLower), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), false, mktypeSelf(*type->pool, sp))), ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, rcstringV), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), false, mktypeSelf(*type->pool, sp)))));
        fcn.setCode(NEWNODE(Block, mv$(node)));

        ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, traitPath, mv$(typesToBound));

        ASTImpl rv(ASTImplDef(mv$(params), makeSpanned(sp, traitPath), type->clone()));
        rv.addFunction(sp, {}, ASTVisibility::makeBarePrivate(), false, RcString::newInterned("partial_cmp"), mv$(fcn));
        return mv$(rv);
    }

    ASTExprNodeP compareAndRet(Span sp, const RcString& coreName, ASTExprNodeP v1, ASTExprNodeP v2) const override {
        return NEWNODE(Match, NEWNODE(CallPath, getPath(coreName, "cmp", "PartialOrd", "partial_cmp"), ::makeVec2(NEWNODE(UniOp, ASTExprNodeUniOp::REF, mv$(v1)), NEWNODE(UniOp, ASTExprNodeUniOp::REF, mv$(v2)))), ::makeVec3(ASTExprNodeMatchArm(::makeVec1(ASTPattern(ASTPattern::TagValue(), sp, getPath(coreName, "option", "Option", "None"))), {}, NEWNODE(Flow, ASTExprNodeFlow::RETURN, "", NEWNODE(NamedValue, getPath(coreName, "option", "Option", "None")))), ASTExprNodeMatchArm(::makeVec1(ASTPattern(ASTPattern::TagNamedTuple(), sp, getPath(coreName, "option", "Option", "Some"), ::makeVec1(ASTPattern(ASTPattern::TagValue(), sp, getPath(coreName, "cmp", "Ordering", "Equal"))))), {}, NEWNODE(Tuple, ::std::vector<ASTExprNodeP>())), ASTExprNodeMatchArm(::makeVec1(ASTPattern(ASTPattern::TagBind(), sp, rcstringRes)), {}, NEWNODE(Flow, ASTExprNodeFlow::RETURN, "", NEWNODE(NamedValue, ASTPath(rcstringRes))))));
    }

    ASTExprNodeP equalValue(Span sp, const RcString& coreName) const override {
        return NEWNODE(CallPath, getPath(coreName, "option", "Option", "Some"), ::makeVec1(NEWNODE(NamedValue, getPath(coreName, "cmp", "Ordering", "Equal"))));
    }

    ASTExprNodeP enumMismatch(Span sp, const RcString& coreName) const override {
        return NEWNODE(CallPath, getPath(coreName, "cmp", "PartialOrd", "partial_cmp"), ::makeVec2(NEWNODE(UniOp, ASTExprNodeUniOp::REF, NEWNODE(CallPath, getPath(coreName, "intrinsics", "discriminant_value"), makeVec1(NEWNODE(NamedValue, ASTPath(rcstringSelfLower))))), NEWNODE(UniOp, ASTExprNodeUniOp::REF, NEWNODE(CallPath, getPath(coreName, "intrinsics", "discriminant_value"), makeVec1(NEWNODE(NamedValue, ASTPath(rcstringV)))))));
    }

public:
    const char* traitName() const override {
        return "PartialOrd";
    }
} gDerivePartialord;

class DeriverEq: public Deriver {
    ASTPath getTraitPath(const RcString& coreName) const {
        return getPath(coreName, "cmp", "Eq");
    }

    ASTImpl makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, ::std::vector<ASTType*> typesToBound, ASTExprNodeP node) const {
        const ASTPath traitPath = this->getTraitPath(coreName);

        ASTFunction fcn(sp, mkType(*type->pool, ASTTypeTags::Unit(), sp), vec$(ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, rcstringSelfLower), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), false, mktypeSelf(*type->pool, sp)))));
        fcn.setCode(NEWNODE(Block, mv$(node)));

        ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, traitPath, mv$(typesToBound));

        ASTImpl rv(ASTImplDef(mv$(params), makeSpanned(sp, traitPath), type->clone()));
        rv.addFunction(sp, {}, ASTVisibility::makeBarePrivate(), false, rcstringAssertReceiverIsTotalEq, mv$(fcn));
        return mv$(rv);
    }

    ASTExprNodeP assertIsEq(const ASTPath& methodPath, ASTExprNodeP val) const {
        return NEWNODE(CallPath, ASTPath(methodPath), vec$(NEWNODE(UniOp, ASTExprNodeUniOp::REF, mv$(val))));
    }

    ASTExprNodeP field(const ::std::string& name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, ASTPath(rcstringSelfLower)), RcString::newInterned(name));
    }

    ASTExprNodeP field(const RcString& name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, ASTPath(rcstringSelfLower)), name);
    }

public:
    const char* traitName() const override {
        return "Eq";
    }

    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const override {
        const ASTPath assertMethodPath = this->getTraitPath(opts.coreName) + rcstringAssertReceiverIsTotalEq;

        auto block = newBlock(sp);
        this->iterateStructFields(str, [&](RcString name) {
            block->pushStmt(this->assertIsEq(assertMethodPath, this->field(name)));
        });

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), mkExprnodep(block.release()));
    }

    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const override {
        const ASTPath assertMethodPath = this->getTraitPath(opts.coreName) + rcstringAssertReceiverIsTotalEq;

        ASTPath basePath = *type->data.as_Path();
        basePath.nodes().back().args() = ASTPathParams();
        ::std::vector<ASTExprNodeMatchArm> arms;

        for (const auto& v : enm.variants()) {
            ASTExprNodeP code;
            ASTPattern patA;
            auto variantPath = basePath + v.name;

            TU_MATCH_HDRA( (v.data), {)
            TU_ARMA(Unit, e) {
                    code = NEWNODE(Block);
                    patA = ASTPattern(ASTPattern::TagValue(), sp, ASTPattern::Value::make_Named(variantPath));
                }
                TU_ARMA(Tuple, e) {
                    auto block = newBlock(sp);
                    ::std::vector<ASTPattern> patsA;
                    makeRefpatA(sp, *block, patsA, e.items, [&](size_t idx, auto a) {
                        return this->assertIsEq(assertMethodPath, mv$(a));
                    });

                    patA = ASTPattern(ASTPattern::TagNamedTuple(), sp, variantPath, mv$(patsA));
                    code = mkExprnodep(block.release());
                }
                TU_ARMA(Struct, e) {
                    auto block = newBlock(sp);
                    ::std::vector<ASTStructPatternEntry> patsA;
                    makeRefpatA(sp, *block, patsA, e.fields, [&](size_t idx, auto a) {
                        return this->assertIsEq(assertMethodPath, mv$(a));
                    });

                    patA = ASTPattern(ASTPattern::TagStruct(), sp, variantPath, mv$(patsA), true);
                    code = mkExprnodep(block.release());
                }
            }

            ::std::vector< ASTPattern>    pats;
            pats.push_back( ASTPattern(ASTPattern::TagReference(), sp, false, mv$(patA)) );

            arms.push_back(ASTExprNodeMatchArm(
                mv$(pats),
                {},
                mv$(code)
                ));
        }

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), NEWNODE(Match, NEWNODE(NamedValue, ASTPath(rcstringSelfLower)), mv$(arms)));
    }

    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTUnion& unn) const override {
        // Eq is just a marker, so it's valid to derive for union
        const ASTPath assertMethodPath = this->getTraitPath(opts.coreName) + rcstringAssertReceiverIsTotalEq;
        auto block = newBlock(sp);

        for (const auto& fld : unn.variants) {
            block->pushStmt(this->assertIsEq(assertMethodPath, this->field(fld.name)));
        }

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(unn), mkExprnodep(block.release()));
    }
} gDeriveEq;

class DeriverOrd: public DeriverInnerCompare {
    ASTImpl makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, ::std::vector<ASTType*> typesToBound, ASTExprNodeP node) const override {
        const ASTPath traitPath = getPath(coreName, "cmp", "Ord");
        const ASTPath pathOrdering = getPath(coreName, "cmp", "Ordering");

        ASTFunction fcn(sp, mkType(*type->pool, sp, pathOrdering), vec$(ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, rcstringSelfLower), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), false, mktypeSelf(*type->pool, sp))), ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, rcstringV), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), false, mktypeSelf(*type->pool, sp)))));
        fcn.setCode(NEWNODE(Block, mv$(node)));

        ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, traitPath, mv$(typesToBound));

        ASTImpl rv(ASTImplDef(mv$(params), makeSpanned(sp, traitPath), type->clone()));
        rv.addFunction(sp, {}, ASTVisibility::makeBarePrivate(), false, RcString::newInterned("cmp"), mv$(fcn));
        return mv$(rv);
    }

    ASTExprNodeP compareAndRet(Span sp, const RcString& coreName, ASTExprNodeP v1, ASTExprNodeP v2) const override {
        return NEWNODE(
            Match,
            NEWNODE(
                CallPath,
                getPath(coreName, "cmp", "Ord", "cmp"),
                // TODO: Optional Ref?
                ::makeVec2(NEWNODE(UniOp, ASTExprNodeUniOp::REF, mv$(v1)), NEWNODE(UniOp, ASTExprNodeUniOp::REF, mv$(v2)))
            ),
            ::makeVec2(ASTExprNodeMatchArm(::makeVec1(ASTPattern(ASTPattern::TagValue(), sp, getPath(coreName, "cmp", "Ordering", "Equal"))), {}, NEWNODE(Tuple, ::std::vector<ASTExprNodeP>())), ASTExprNodeMatchArm(::makeVec1(ASTPattern(ASTPattern::TagBind(), sp, rcstringRes)), {}, NEWNODE(Flow, ASTExprNodeFlow::RETURN, "", NEWNODE(NamedValue, ASTPath(rcstringRes)))))
        );
    }

    ASTExprNodeP equalValue(Span sp, const RcString& coreName) const override {
        return NEWNODE(NamedValue, getPath(coreName, "cmp", "Ordering", "Equal"));
    }

    ASTExprNodeP enumMismatch(Span sp, const RcString& coreName) const override {
        return NEWNODE(CallPath, getPath(coreName, "cmp", "Ord", "cmp"), ::makeVec2(NEWNODE(UniOp, ASTExprNodeUniOp::REF, NEWNODE(CallPath, getPath(coreName, "intrinsics", "discriminant_value"), makeVec1(NEWNODE(NamedValue, ASTPath(rcstringSelfLower))))), NEWNODE(UniOp, ASTExprNodeUniOp::REF, NEWNODE(CallPath, getPath(coreName, "intrinsics", "discriminant_value"), makeVec1(NEWNODE(NamedValue, ASTPath(rcstringV)))))));
    }

public:
    const char* traitName() const override {
        return "Ord";
    }
} gDeriveOrd;

class DeriverClone: public Deriver {
    ASTPath getTraitPath(const RcString& coreName) const {
        return ASTPath(coreName, {ASTPathNode(rcstringCloneLower, {}), ASTPathNode(rcstringClone, {})});
    }

    ASTPath getMethodPath(const RcString& coreName) const {
        return getTraitPath(coreName) + rcstringCloneLower;
    }

    ASTImpl makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, ::std::vector<ASTType*> typesToBound, ASTExprNodeP node) const {
        const ASTPath traitPath = this->getTraitPath(coreName);

        ASTFunction fcn(sp, mktypeSelf(*type->pool, sp), vec$(ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, rcstringSelfLower), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), false, mktypeSelf(*type->pool, sp)))));
        fcn.setCode(NEWNODE(Block, mv$(node)));

        ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, traitPath, mv$(typesToBound));

        ASTImpl rv(ASTImplDef(mv$(params), makeSpanned(sp, traitPath), type->clone()));
        rv.addFunction(sp, {}, ASTVisibility::makeBarePrivate(), false, rcstringCloneLower, mv$(fcn));
        return mv$(rv);
    }

    ASTExprNodeP cloneValRef(const RcString& coreName, ASTExprNodeP val) const {
        // TODO: Hack for zero-sized arrays? (Not a 1.19 feature)
        return NEWNODE(CallPath, this->getMethodPath(coreName), vec$(NEWNODE(UniOp, ASTExprNodeUniOp::REF, mv$(val))));
    }

    ASTExprNodeP cloneValDirect(const RcString& coreName, ASTExprNodeP val) const {
        return NEWNODE(CallPath, this->getMethodPath(coreName), vec$(mv$(val)));
    }

    ASTExprNodeP field(const RcString& name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, ASTPath(rcstringSelfLower)), name);
    }

    ASTExprNodeP field(const ::std::string& name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, ASTPath(rcstringSelfLower)), RcString::newInterned(name));
    }

public:
    const char* traitName() const override {
        return "Clone";
    }

    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const override {
        const ASTPath& tyPath = *type->data.as_Path();

        // A `Copy` type clones by copying. Cloning field by field would call a
        // field's own `Clone` impl, which is observable when it does more than
        // copy. A generic type is left alone: `*self` needs `Self: Copy`, which
        // the derived `Clone` bounds do not give.
        if (opts.derivesCopy && p.params.empty()) {
            return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), NEWNODE(Block, NEWNODE(Deref, NEWNODE(NamedValue, ASTPath(rcstringSelfLower)))));
        }

        ASTExprNodeP node;
        TU_MATCH_HDRA( (str.data), {)
        TU_ARMA(Unit, e) {
                node = NEWNODE(NamedValue, ASTPath(tyPath));
            }
            TU_ARMA(Struct, e) {
                ASTExprNodeStructLiteral::tValues vals;
                for (const auto& fld : e.ents) {
                    vals.push_back({{}, fld.name, this->cloneValRef(opts.coreName, this->field(fld.name))});
                }
                node = NEWNODE(StructLiteral, tyPath, nullptr, mv$(vals));
            }
            TU_ARMA(Tuple, e) {
                ::std::vector<ASTExprNodeP> vals;
                for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                    vals.push_back(this->cloneValRef(opts.coreName, this->field(FMT(idx))));
                }
                node = NEWNODE(CallPath, ASTPath(tyPath), mv$(vals));
            }
        }

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), NEWNODE(Block, mv$(node)));
    }

    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const override {
        if (opts.derivesCopy && p.params.empty()) {
            return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), NEWNODE(Block, NEWNODE(Deref, NEWNODE(NamedValue, ASTPath(rcstringSelfLower)))));
        }
        ASTPath basePath = *type->data.as_Path();
        basePath.nodes().back().args() = ASTPathParams();
        ::std::vector<ASTExprNodeMatchArm> arms;

        for (const auto& v : enm.variants()) {
            ASTExprNodeP code;
            ASTPattern patA;

            TU_MATCH_HDRA( (v.data), {)
            TU_ARMA(Unit, e) {
                    code = NEWNODE(NamedValue, basePath + v.name);
                    patA = ASTPattern(ASTPattern::TagValue(), sp, ASTPattern::Value::make_Named(basePath + v.name));
                }
                TU_ARMA(Tuple, e) {
                    ::std::vector<ASTPattern> patsA;
                    ::std::vector<ASTExprNodeP> nodes;

                    for (size_t idx = 0; idx < e.items.size(); idx++) {
                        auto nameA = RcString::newInterned(FMT("a" << idx));
                        patsA.push_back(ASTPattern(ASTPattern::TagBind(), sp, nameA, ASTPatternBinding::Type::REF));
                        nodes.push_back(this->cloneValDirect(opts.coreName, NEWNODE(NamedValue, ASTPath(nameA))));
                    }

                    patA = ASTPattern(ASTPattern::TagNamedTuple(), sp, basePath + v.name, mv$(patsA));
                    code = NEWNODE(CallPath, basePath + v.name, mv$(nodes));
                }
                TU_ARMA(Struct, e) {
                    ::std::vector<ASTStructPatternEntry> patsA;
                    ASTExprNodeStructLiteral::tValues vals;

                    for (const auto& fld : e.fields) {
                        auto nameA = RcString::newInterned(FMT("a" << fld.name));
                        patsA.push_back(ASTStructPatternEntry{ASTAttributeList(), fld.name, ASTPattern(ASTPattern::TagBind(), sp, nameA, ASTPatternBinding::Type::REF)});
                        vals.push_back({{}, fld.name, this->cloneValDirect(opts.coreName, NEWNODE(NamedValue, ASTPath(nameA)))});
                    }

                    patA = ASTPattern(ASTPattern::TagStruct(), sp, basePath + v.name, mv$(patsA), true);
                    code = NEWNODE(StructLiteral, basePath + v.name, nullptr, mv$(vals));
                }
            }

            ::std::vector< ASTPattern>    pats;
            pats.push_back( ASTPattern(ASTPattern::TagReference(), sp, false, mv$(patA)) );

            arms.push_back(ASTExprNodeMatchArm(
                mv$(pats),
                {},
                mv$(code)
                ));
        }

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), NEWNODE(Match, NEWNODE(NamedValue, ASTPath(rcstringSelfLower)), mv$(arms)));
    }

    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTUnion& unn) const override {
        return makeCopyClone(sp, opts, p, type, this->getFieldBounds(unn));
    }

private:
    ASTImpl makeCopyClone(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, ::std::vector<ASTType*> fieldBounds) const {
        // Clone on a union can only be a bitwise copy.
        // - This requires a Copy impl. That's up to the user
        auto ret = this->makeRet(sp, opts.coreName, p, type, ::std::move(fieldBounds), NEWNODE(Deref, NEWNODE(NamedValue, ASTPath(rcstringSelfLower))));

        // TODO: What if the type is only conditionally copy? (generic over something)
        // - Could abuse specialisation support...
        // TODO: Are these bounds needed?
        for (auto& b : ret.def().params().bounds) {
            auto& be = b.as_IsTrait();
            be.trait = getPath(opts.coreName, "marker", "Copy");
        }

        return ret;
    }
} gDeriveClone;

class DeriverCopy: public Deriver {
    ASTPath getTraitPath(const RcString& coreName) const {
        return getPath(coreName, "marker", "Copy");
    }

    ASTImpl makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, ::std::vector<ASTType*> typesToBound, ASTExprNodeP node) const {
        const ASTPath traitPath = this->getTraitPath(coreName);

        ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, traitPath, mv$(typesToBound));

        ASTImpl rv(ASTImplDef(mv$(params), makeSpanned(sp, traitPath), type->clone()));
        return mv$(rv);
    }

public:
    const char* traitName() const override {
        return "Copy";
    }

    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const override {
        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), nullptr);
    }

    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const override {
        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), nullptr);
    }

    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTUnion& unn) const override {
        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(unn), nullptr);
    }
} gDeriveCopy;

class DeriverDefault: public Deriver {
    ASTPath getTraitPath(const RcString& coreName) const {
        return getPath(coreName, "default", "Default");
    }

    ASTPath getMethodPath(stl::ObjPool& pool, const RcString& coreName) const {
        return ASTPath::newUfcsTrait(::mkType(pool, Span()), getTraitPath(coreName), {ASTPathNode(RcString::newInterned("default"), {})});
    }

    ASTImpl makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, ::std::vector<ASTType*> typesToBound, ASTExprNodeP node, bool boundTypeParams = true) const {
        const ASTPath traitPath = this->getTraitPath(coreName);

        ASTFunction fcn(sp, mktypeSelf(*type->pool, sp), {});
        fcn.setCode(NEWNODE(Block, mv$(node)));

        ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, traitPath, mv$(typesToBound), boundTypeParams);

        ASTImpl rv(ASTImplDef(mv$(params), makeSpanned(sp, traitPath), type->clone()));
        rv.addFunction(sp, {}, ASTVisibility::makeBarePrivate(), false, RcString::newInterned("default"), mv$(fcn));
        return mv$(rv);
    }

    ASTExprNodeP defaultCall(stl::ObjPool& pool, const RcString& coreName) const {
        return NEWNODE(CallPath, this->getMethodPath(pool, coreName), {});
    }

public:
    const char* traitName() const override {
        return "Default";
    }

    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const override {
        const ASTPath& tyPath = *type->data.as_Path();
        ASTExprNodeP node;

        TU_MATCH_HDRA( (str.data), {)
        TU_ARMA(Unit, e) {
                node = NEWNODE(NamedValue, ASTPath(tyPath));
            }
            TU_ARMA(Struct, e) {
                ASTExprNodeStructLiteral::tValues vals;
                bool hasDefault = false;
                for (const auto& fld : e.ents) {
                    if (fld.defaultValue) {
                        hasDefault = true;
                    } else {
                        vals.push_back({{}, fld.name, this->defaultCall(*type->pool, opts.coreName)});
                    }
                }
                if (hasDefault) {
                    node = NEWNODE(StructLiteralPattern, tyPath, mv$(vals));
                } else {
                    node = NEWNODE(StructLiteral, tyPath, nullptr, mv$(vals));
                }
            }
            TU_ARMA(Tuple, e) {
                ::std::vector<ASTExprNodeP> vals;
                for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                    vals.push_back(this->defaultCall(*type->pool, opts.coreName));
                }
                node = NEWNODE(CallPath, ASTPath(tyPath), mv$(vals));
            }
        }

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), NEWNODE(Block, mv$(node)));
    }

    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const override {
        // 1.74: #[default]
        const ASTEnumVariant* defaultVar = nullptr;
        for (const auto& v : enm.variants()) {
            if (v.attrs.has("default")) {
                if (defaultVar) {
                    ERROR(sp, E0000, "Multiple #[default] attributes");
                }
                defaultVar = &v;
            }
        }
        if (!defaultVar) {
            ERROR(sp, E0000, "No #[default] attribute on enum with derive(Default)");
        }

        ASTPath varPath = *type->data.as_Path() + ASTPathNode(defaultVar->name);

        ::std::vector<ASTType*> boundTys;
        ASTExprNodeP node;
        TU_MATCH_HDRA( (defaultVar->data), { )
        TU_ARMA(Unit, e) {
                node = NEWNODE(NamedValue, std::move(varPath));
            }
            TU_ARMA(Tuple, e) {
                ::std::vector<ASTExprNodeP> vals;
                for (const auto& fld : e.items) {
                    addFieldBoundFromTy(enm.params(), boundTys, fld.type);
                    vals.push_back(this->defaultCall(*type->pool, opts.coreName));
                }
                node = NEWNODE(CallPath, std::move(varPath), mv$(vals));
            }
            TU_ARMA(Struct, e) {
                ASTExprNodeStructLiteral::tValues vals;
                for (const auto& fld : e.fields) {
                    if (fld.defaultValue) {
                    } else {
                        addFieldBoundFromTy(enm.params(), boundTys, fld.type);
                        vals.push_back({{}, fld.name, this->defaultCall(*type->pool, opts.coreName)});
                    }
                }
                node = NEWNODE(StructLiteralPattern, std::move(varPath), mv$(vals));
            }
        }
        // Only the `#[default]` variant is constructed, so the other variants'
        // types need no bound -- `MyOption<NotDefault>::default()` is fine when
        // the default variant carries nothing.
        return this->makeRet(sp, opts.coreName, p, type, std::move(boundTys), std::move(node), /*boundTypeParams=*/false);
    }
} gDeriveDefault;

class DeriverHash: public Deriver {
    ASTPath getTraitPath(const RcString& coreName) const {
        return getPath(coreName, "hash", "Hash");
    }

    ASTPath getTraitPathHasher(const RcString& coreName) const {
        return getPath(coreName, "hash", "Hasher");
    }

    ASTPath getMethodPath(const RcString& coreName) const {
        return getTraitPath(coreName) + RcString::newInterned("hash");
    }

    ASTImpl makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, ::std::vector<ASTType*> typesToBound, ASTExprNodeP node) const {
        const ASTPath traitPath = this->getTraitPath(coreName);

        ASTFunction fcn(sp, mkType(*type->pool, ASTTypeTags::Unit(), sp), vec$(ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, rcstringSelfLower), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), false, mktypeSelf(*type->pool, sp))), ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, rcstringState), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), true, mkType(*type->pool, sp, rcstringH, 0x100 | 0)))));
        fcn.params().addTyParam(ASTTypeParam(*type->pool, sp, {}, rcstringH));
        fcn.params().addBound(ASTGenericBound::make_IsTrait({sp, {}, mkType(*type->pool, sp, rcstringH, 0x100 | 0), {}, this->getTraitPathHasher(coreName)}));
        fcn.setCode(NEWNODE(Block, mv$(node)));

        ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, traitPath, mv$(typesToBound));

        ASTImpl rv(ASTImplDef(mv$(params), makeSpanned(sp, traitPath), type->clone()));
        rv.addFunction(sp, {}, ASTVisibility::makeBarePrivate(), false, RcString::newInterned("hash"), mv$(fcn));
        return mv$(rv);
    }

    ASTExprNodeP hashValRef(const RcString& coreName, ASTExprNodeP val) const {
        return this->hashValDirect(coreName, NEWNODE(UniOp, ASTExprNodeUniOp::REF, mv$(val)));
    }

    ASTExprNodeP hashValDirect(const RcString& coreName, ASTExprNodeP val) const {
        return NEWNODE(CallPath, this->getMethodPath(coreName), vec$(mv$(val), NEWNODE(NamedValue, ASTPath(rcstringState))));
    }

    ASTExprNodeP field(const RcString& name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, ASTPath(rcstringSelfLower)), name);
    }

    ASTExprNodeP field(const std::string& name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, ASTPath(rcstringSelfLower)), RcString::newInterned(name));
    }

public:
    const char* traitName() const override {
        return "Hash";
    }

    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const override {
        auto block = newBlock(sp);

        TU_MATCH_HDRA( (str.data), {)
        TU_ARMA(Unit, e) {
            }
            TU_ARMA(Struct, e) {
                for (const auto& fld : e.ents) {
                    block->pushStmt(this->hashValRef(opts.coreName, this->field(fld.name)));
                }
            }
            TU_ARMA(Tuple, e) {
                for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                    block->pushStmt(this->hashValRef(opts.coreName, this->field(FMT(idx))));
                }
            }
        }

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), mkExprnodep(block.release()));
    }

    /// The type of an enum's discriminant: whatever `#[repr]` names, or `isize`.
    static eCoreType discriminantCoreType(const ASTEnum& enm) {
        switch (enm.markings.repr) {
            case ASTEnum::Markings::Repr::Rust:
                return CORETYPE_INT;
            case ASTEnum::Markings::Repr::U8:
                return CORETYPE_U8;
            case ASTEnum::Markings::Repr::U16:
                return CORETYPE_U16;
            case ASTEnum::Markings::Repr::U32:
                return CORETYPE_U32;
            case ASTEnum::Markings::Repr::U64:
                return CORETYPE_U64;
            case ASTEnum::Markings::Repr::U128:
                return CORETYPE_U128;
            case ASTEnum::Markings::Repr::Usize:
                return CORETYPE_UINT;
            case ASTEnum::Markings::Repr::I8:
                return CORETYPE_I8;
            case ASTEnum::Markings::Repr::I16:
                return CORETYPE_I16;
            case ASTEnum::Markings::Repr::I32:
                return CORETYPE_I32;
            case ASTEnum::Markings::Repr::I64:
                return CORETYPE_I64;
            case ASTEnum::Markings::Repr::I128:
                return CORETYPE_I128;
            case ASTEnum::Markings::Repr::Isize:
                return CORETYPE_INT;
        }
        return CORETYPE_INT;
    }

    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const override {
        ASTPath basePath = *type->data.as_Path();
        basePath.nodes().back().args() = ASTPathParams();
        ::std::vector<ASTExprNodeMatchArm> arms;

        for (unsigned int varIdx = 0; varIdx < enm.variants().size(); varIdx++) {
            const auto& v = enm.variants()[varIdx];
            ASTPattern patA;

            auto varPath = basePath + v.name;
            // The discriminant is hashed at its own width: an `#[repr(u8)]` enum
            // writes one byte, not a whole `isize`.
            auto varIdxHash = enm.variants().size() > 1 ? this->hashValRef(opts.coreName, NEWNODE(Integer, U128(varIdx), discriminantCoreType(enm))) : NEWNODE(Tuple, {});

            auto block = newBlock(sp);
            block->pushStmt(mv$(varIdxHash));
            TU_MATCH_HDRA( (v.data), {)
            TU_ARMA(Unit, e) {
                    patA = ASTPattern(ASTPattern::TagValue(), sp, ASTPattern::Value::make_Named(varPath));
                }
                TU_ARMA(Tuple, e) {
                    ::std::vector<ASTPattern> patsA;
                    makeRefpatA(sp, *block, patsA, e.items, [&](size_t, auto a) {
                        return this->hashValDirect(opts.coreName, mv$(a));
                    });
                    patA = ASTPattern(ASTPattern::TagNamedTuple(), sp, varPath, mv$(patsA));
                }
                TU_ARMA(Struct, e) {
                    ::std::vector<ASTStructPatternEntry> patsA;
                    makeRefpatA(sp, *block, patsA, e.fields, [&](size_t, auto a) {
                        return this->hashValDirect(opts.coreName, mv$(a));
                    });
                    patA = ASTPattern(ASTPattern::TagStruct(), sp, varPath, mv$(patsA), true);
                }
            }

            ::std::vector< ASTPattern>    pats;
            pats.push_back( ASTPattern(ASTPattern::TagReference(), sp, false, mv$(patA)) );

            arms.push_back(ASTExprNodeMatchArm(
                mv$(pats),
                {},
                mkExprnodep(block.release())
                ));
        }

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), NEWNODE(Match, NEWNODE(NamedValue, ASTPath(rcstringSelfLower)), mv$(arms)));
    }
} gDeriveHash;

class DeriverRustcEncodable: public Deriver {
    // NOTE: This emits paths like `::rustc_serialize::Encodable` - rustc and crates.io have subtly different crate names
    ASTPath getTraitPath() const {
        return ASTPath(RcString::newInterned("=rustc_serialize"), {ASTPathNode(RcString::newInterned("Encodable"), {})});
    }

    ASTPath getTraitPathEncoder() const {
        return ASTPath(RcString::newInterned("=rustc_serialize"), {ASTPathNode(RcString::newInterned("Encoder"), {})});
    }

    ASTPath getMethodPath() const {
        return getTraitPath() + "encode";
    }

    ASTImpl makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, ::std::vector<ASTType*> typesToBound, ASTExprNodeP node) const {
        const ASTPath traitPath = this->getTraitPath();

        ASTPath resultPath = getPath(coreName, "result", "Result");
        resultPath.nodes()[1].args().entries.push_back(mkType(*type->pool, ASTTypeTags::Unit(), sp));
        resultPath.nodes()[1].args().entries.push_back(mkType(*type->pool, sp, ASTPath::newUfcsTrait(mkType(*type->pool, sp, "S", 0x100 | 0), this->getTraitPathEncoder(), {ASTPathNode("Error", {})})));

        ASTFunction fcn(sp, mkType(*type->pool, sp, mv$(resultPath)), vec$(ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, rcstringSelfLower), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), false, mktypeSelf(*type->pool, sp))), ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, rcstringS), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), true, mkType(*type->pool, sp, RcString::newInterned("S"), 0x100 | 0)))));
        fcn.params().addTyParam(ASTTypeParam(*type->pool, sp, {}, "S"));
        fcn.params().addBound(ASTGenericBound::make_IsTrait({sp, {}, mkType(*type->pool, sp, "S", 0x100 | 0), {}, this->getTraitPathEncoder()}));
        fcn.setCode(NEWNODE(Block, mv$(node)));

        ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, traitPath, mv$(typesToBound));

        ASTImpl rv(ASTImplDef(mv$(params), makeSpanned(sp, traitPath), type->clone()));
        rv.addFunction(sp, {}, ASTVisibility::makeBarePrivate(), false, "encode", mv$(fcn));
        return mv$(rv);
    }

    ASTExprNodeP encValDirect(ASTExprNodeP val) const {
        return NEWNODE(CallPath, this->getMethodPath(), vec$(mv$(val), NEWNODE(NamedValue, ASTPath(rcstringS))));
    }

    ASTExprNodeP encValRef(ASTExprNodeP val) const {
        return this->encValDirect(NEWNODE(UniOp, ASTExprNodeUniOp::REF, mv$(val)));
    }

    ASTExprNodeP field(const RcString& name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, ASTPath(rcstringSelfLower)), name);
    }

    ASTExprNodeP field(::std::string name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, ASTPath(rcstringSelfLower)), RcString::newInterned(name));
    }

    ASTExprNodeP encClosure(stl::ObjPool& pool, Span sp, ASTExprNodeP code) const {
        return NEWNODE(Closure, vec$(::std::make_pair(ASTPattern(ASTPattern::TagBind(), sp, rcstringS), ::mkType(pool, sp))), ::mkType(pool, sp), mv$(code), false, false, false);
    }

    ASTExprNodeP getValOk(const RcString& coreName) const {
        return NEWNODE(CallPath, getPath(coreName, "result", "Result", "Ok"), vec$(NEWNODE(Tuple, {})));
    }

public:
    const char* traitName() const override {
        return "RustcEncodable";
    }

    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const override {
        ::std::string structName = type->data.as_Path()->nodes().back().name().c_str();

        auto block = newBlock(sp);
        TU_MATCH_HDRA( (str.data), {)
        TU_ARMA(Unit, e) {
            }
            TU_ARMA(Struct, e) {
                unsigned int idx = 0;
                for (const auto& fld : e.ents) {
                    block->pushStmt(NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_struct_field", vec$(NEWNODE(NamedValue, ASTPath(rcstringS)), NEWNODE(String, fld.name.c_str()), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->encClosure(*type->pool, sp, this->encValRef(this->field(fld.name))))));
                    idx++;
                }
            }
            TU_ARMA(Tuple, e) {
                for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                    block->pushStmt(NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_tuple_struct_arg", vec$(NEWNODE(NamedValue, ASTPath(rcstringS)), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->encClosure(*type->pool, sp, this->encValRef(this->field(FMT(idx)))))));
                }
            }
        }

        block->pushTailExpr( this->getValOk(opts.coreName) );
        auto closure = this->encClosure(*type->pool,  sp, mkExprnodep(block.release()) );

        ASTExprNodeP    node;
        TU_MATCH_HDRA( (str.data), {)
        TU_ARMA(Unit, e) {
                node = getValOk(opts.coreName);
            }
            TU_ARMA(Struct, e) {
                node = NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_struct", vec$(NEWNODE(NamedValue, ASTPath(rcstringS)), NEWNODE(String, structName), NEWNODE(Integer, U128(e.ents.size()), CORETYPE_UINT), mv$(closure)));
            }
            TU_ARMA(Tuple, e) {
                node = NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_tuple_struct", vec$(NEWNODE(NamedValue, ASTPath(rcstringS)), NEWNODE(String, structName), NEWNODE(Integer, U128(e.ents.size()), CORETYPE_UINT), mv$(closure)));
            }
        }

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), mv$(node));
    }

    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const override {
        ASTPath basePath = *type->data.as_Path();
        basePath.nodes().back().args() = ASTPathParams();
        ::std::vector<ASTExprNodeMatchArm> arms;

        auto sEnt = NEWNODE(NamedValue, ASTPath(rcstringS));

        for (unsigned int varIdx = 0; varIdx < enm.variants().size(); varIdx++) {
            const auto& v = enm.variants()[varIdx];
            ASTExprNodeP code;
            ASTPattern patA;

            TU_MATCH_HDRA((v.data), {)
            TU_ARMA(Unit, e) {
                    code = NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_enum_variant", vec$(sEnt->clone(), NEWNODE(String, v.name.c_str()), NEWNODE(Integer, U128(varIdx), CORETYPE_UINT), NEWNODE(Integer, U128(0), CORETYPE_UINT), this->encClosure(*type->pool, sp, this->getValOk(opts.coreName))));
                    patA = ASTPattern(ASTPattern::TagValue(), sp, ASTPattern::Value::make_Named(basePath + v.name));
                }
                TU_ARMA(Tuple, e) {
                    auto block = newBlock(sp);
                    ::std::vector<ASTPattern> patsA;
                    makeRefpatA(sp, *block, patsA, e.items, [&](size_t idx, auto a) {
                        return NEWNODE(CallPath, this->getTraitPathEncoder() + RcString::newInterned("emit_enum_variant_arg"), vec$(sEnt->clone(), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->encClosure(*type->pool, sp, this->encValDirect(mv$(a)))));
                    });
                    block->pushTailExpr(this->getValOk(opts.coreName));

                    code = NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_enum_variant", vec$(sEnt->clone(), NEWNODE(String, v.name.c_str()), NEWNODE(Integer, U128(varIdx), CORETYPE_UINT), NEWNODE(Integer, U128(e.items.size()), CORETYPE_UINT), this->encClosure(*type->pool, sp, mkExprnodep(block.release()))));
                    patA = ASTPattern(ASTPattern::TagNamedTuple(), sp, basePath + v.name, mv$(patsA));
                }
                TU_ARMA(Struct, e) {
                    auto block = newBlock(sp);
                    ::std::vector<ASTStructPatternEntry> patsA;
                    makeRefpatA(sp, *block, patsA, e.fields, [&](size_t idx, auto a) {
                        return NEWNODE(CallPath, this->getTraitPathEncoder() + RcString::newInterned("emit_enum_struct_variant_field"), vec$(sEnt->clone(), NEWNODE(String, e.fields[idx].name.c_str()), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->encClosure(*type->pool, sp, this->encValDirect(mv$(a)))));
                    });
                    block->pushTailExpr(this->getValOk(opts.coreName));

                    patA = ASTPattern(ASTPattern::TagStruct(), sp, basePath + v.name, mv$(patsA), true);
                    code = NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_enum_struct_variant", vec$(sEnt->clone(), NEWNODE(String, v.name.c_str()), NEWNODE(Integer, U128(varIdx), CORETYPE_UINT), NEWNODE(Integer, U128(e.fields.size()), CORETYPE_UINT), this->encClosure(*type->pool, sp, mkExprnodep(block.release()))));
                }
            }

            ::std::vector< ASTPattern>    pats;
            pats.push_back( ASTPattern(ASTPattern::TagReference(), sp, false, mv$(patA)) );

            arms.push_back(ASTExprNodeMatchArm(
                mv$(pats),
                {},
                mv$(code)
                ));
        }

        auto nodeMatch = NEWNODE(Match, NEWNODE(NamedValue, ASTPath(rcstringSelfLower)), mv$(arms));

        ::std::string enumName = type->data.as_Path()->nodes().back().name().c_str();
        auto node = NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_enum", vec$(mv$(sEnt), NEWNODE(String, enumName), this->encClosure(*type->pool, sp, mv$(nodeMatch))));

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), mv$(node));
    }
} gDeriveRustcEncodable;

class DeriverRustcDecodable: public Deriver {
    // NOTE: This emits paths like `::rustc_serialize::Encodable` - rustc and crates.io have subtly different crate names
    ASTPath getTraitPath() const {
        return ASTPath(RcString::newInterned("=rustc_serialize"), {ASTPathNode(RcString::newInterned("Decodable"), {})});
    }

    ASTPath getTraitPathDecoder() const {
        return ASTPath(RcString::newInterned("=rustc_serialize"), {ASTPathNode(RcString::newInterned("Decoder"), {})});
    }

    ASTPath getMethodPath() const {
        return getTraitPath() + "decode";
    }

    ASTImpl makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, ::std::vector<ASTType*> typesToBound, ASTExprNodeP node) const {
        const ASTPath traitPath = this->getTraitPath();

        ASTPath resultPath = getPath(coreName, "result", "Result");
        resultPath.nodes()[1].args().entries.push_back(mktypeSelf(*type->pool, sp));
        resultPath.nodes()[1].args().entries.push_back(mkType(*type->pool, sp, ASTPath::newUfcsTrait(mkType(*type->pool, sp, "D", 0x100 | 0), this->getTraitPathDecoder(), {ASTPathNode("Error", {})})));

        ASTFunction fcn(
            sp,
            mkType(*type->pool, sp, resultPath),
            vec$(
                //AST::Function::Arg( AST::Pattern(AST::Pattern::TagBind(), sp, rcstring_self), mkType(*type->pool, ASTTypeTags::Reference(), sp, false, AST::LifetimeRef(), mktype_Self(sp)) ),
                ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, "d"), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), true, mkType(*type->pool, sp, "D", 0x100 | 0)))
            )
        );
        fcn.params().addTyParam(ASTTypeParam(*type->pool, sp, {}, "D"));
        fcn.params().addBound(ASTGenericBound::make_IsTrait({sp, {}, mkType(*type->pool, sp, "D", 0x100 | 0), {}, this->getTraitPathDecoder()}));
        fcn.setCode(NEWNODE(Block, mv$(node)));

        ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, traitPath, mv$(typesToBound));

        ASTImpl rv(ASTImplDef(mv$(params), makeSpanned(sp, traitPath), type->clone()));
        rv.addFunction(sp, {}, ASTVisibility::makeBarePrivate(), false, "decode", mv$(fcn));
        return mv$(rv);
    }

    ASTExprNodeP decVal() const {
        return NEWNODE(CallPath, this->getMethodPath(), vec$(NEWNODE(NamedValue, ASTPath("d"))));
    }

    ASTExprNodeP field(const ::std::string& name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, ASTPath(rcstringSelfLower)), RcString::newInterned(name));
    }

    ASTExprNodeP decClosure(stl::ObjPool& pool, Span sp, ASTExprNodeP code) const {
        return NEWNODE(Closure, vec$(::std::make_pair(ASTPattern(ASTPattern::TagBind(), sp, "d"), ::mkType(pool, sp))), ::mkType(pool, sp), mv$(code), false, false, false);
    }

    ASTExprNodeP getValErrStr(const RcString& coreName, ::std::string errStr) const {
        return NEWNODE(CallPath, getPath(coreName, "result", "Result", "Err"), vec$(NEWNODE(CallMethod, NEWNODE(NamedValue, ASTPath("d")), ASTPathNode("error"), vec$(NEWNODE(String, errStr)))));
    }

    ASTExprNodeP getValOk(const RcString& coreName, ASTExprNodeP inner) const {
        return NEWNODE(CallPath, getPath(coreName, "result", "Result", "Ok"), vec$(mv$(inner)));
    }

    ASTExprNodeP getValOkUnit(const RcString& coreName) const {
        return getValOk(coreName, NEWNODE(Tuple, {}));
    }

public:
    const char* traitName() const override {
        return "RustcDecodable";
    }

    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const override {
        ASTPath basePath = *type->data.as_Path();
        ::std::string structName = basePath.nodes().back().name().c_str();

        ASTExprNodeP nodeV;
        TU_MATCH_HDRA((str.data), {)
        TU_ARMA(Unit, e) {
            }
            TU_ARMA(Struct, e) {
                ASTExprNodeStructLiteral::tValues vals;
                unsigned int idx = 0;
                for (const auto& fld : e.ents) {
                    vals.push_back({{}, fld.name, NEWNODE(UniOp, ASTExprNodeUniOp::QMARK, NEWNODE(CallPath, this->getTraitPathDecoder() + "read_struct_field", vec$(NEWNODE(NamedValue, ASTPath("d")), NEWNODE(String, fld.name.c_str()), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->decClosure(*type->pool, sp, this->decVal()))))});
                    idx++;
                }
                nodeV = NEWNODE(StructLiteral, basePath, nullptr, mv$(vals));
            }
            TU_ARMA(Tuple, e) {
                ::std::vector<ASTExprNodeP> vals;
                for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                    vals.push_back(NEWNODE(UniOp, ASTExprNodeUniOp::QMARK, NEWNODE(CallPath, this->getTraitPathDecoder() + "read_tuple_struct_arg", vec$(NEWNODE(NamedValue, ASTPath("d")), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->decClosure(*type->pool, sp, this->decVal())))));
                }
                nodeV = NEWNODE(CallPath, mv$(basePath), mv$(vals));
            }
        }

        auto closure = this->decClosure(*type->pool,  sp, this->getValOk(opts.coreName, mv$(nodeV)) );

        auto args = vec$( NEWNODE(NamedValue, ASTPath("d")), NEWNODE(String, structName), ASTExprNodeP(), mv$(closure) );

        ASTExprNodeP    node;
        TU_MATCH_HDRA((str.data), {)
        TU_ARMA(Unit, e) {
                node = this->getValOk(opts.coreName, NEWNODE(NamedValue, mv$(basePath)));
            }
            TU_ARMA(Struct, e) {
                assert(!args[2]);
                args[2] = NEWNODE(Integer, U128(e.ents.size()), CORETYPE_UINT);
                node = NEWNODE(CallPath, this->getTraitPathDecoder() + "read_struct", mv$(args));
            }
            TU_ARMA(Tuple, e) {
                assert(!args[2]);
                args[2] = NEWNODE(Integer, U128(e.ents.size()), CORETYPE_UINT);
                node = NEWNODE(CallPath, this->getTraitPathDecoder() + "read_tuple_struct", mv$(args));
            }
        }

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), mv$(node));
    }

    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const override {
        ASTPath basePath = *type->data.as_Path();
        basePath.nodes().back().args() = ASTPathParams();
        ::std::vector<ASTExprNodeMatchArm> arms;

        // 1. Variant names
        ::std::vector<ASTExprNodeP> varNameStrs;

        // 2. Decoding arms
        for (unsigned int varIdx = 0; varIdx < enm.variants().size(); varIdx++) {
            const auto& v = enm.variants()[varIdx];
            ASTExprNodeP code;

            TU_MATCH_HDRA( (v.data), {)
            TU_ARMA(Unit, e) {
                    code = NEWNODE(NamedValue, basePath + v.name);
                }
                TU_ARMA(Tuple, e) {
                    ::std::vector<ASTExprNodeP> args;

                    for (unsigned int idx = 0; idx < e.items.size(); idx++) {
                        args.push_back(NEWNODE(UniOp, ASTExprNodeUniOp::QMARK, NEWNODE(CallPath, this->getTraitPathDecoder() + "read_enum_variant_arg", vec$(NEWNODE(NamedValue, ASTPath("d")), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->decClosure(*type->pool, sp, this->decVal())))));
                    }
                    code = NEWNODE(CallPath, basePath + v.name, mv$(args));
                }
                TU_ARMA(Struct, e) {
                    ASTExprNodeStructLiteral::tValues vals;

                    unsigned int idx = 0;
                    for (const auto& fld : e.fields) {
                        vals.push_back({{}, fld.name, NEWNODE(UniOp, ASTExprNodeUniOp::QMARK, NEWNODE(CallPath, this->getTraitPathDecoder() + "read_enum_struct_variant_field", vec$(NEWNODE(NamedValue, ASTPath("d")), NEWNODE(String, fld.name.c_str()), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->decClosure(*type->pool, sp, this->decVal()))))});
                        idx++;
                    }

                    code = NEWNODE(StructLiteral, basePath + v.name, nullptr, mv$(vals));
                }
            }

            ::std::vector< ASTPattern>    pats;
            pats.push_back( ASTPattern(ASTPattern::TagValue(), sp, ASTPattern::Value::make_Integer({CORETYPE_UINT, U128(varIdx)})) );

            arms.push_back(ASTExprNodeMatchArm(
                mv$(pats),
                {},
                this->getValOk(opts.coreName, mv$(code))
                ));
            varNameStrs.push_back( NEWNODE(String, v.name.c_str()) );
        }

        // Default arm
        {
            arms.push_back(ASTExprNodeMatchArm(::makeVec1(ASTPattern()), {}, this->getValErrStr(opts.coreName, "enum value unknown")));
        }

        auto nodeMatch = NEWNODE(Match, NEWNODE(NamedValue, ASTPath("idx")), mv$(arms));
        auto nodeVarClosure = NEWNODE(Closure, vec$(::std::make_pair(ASTPattern(ASTPattern::TagBind(), sp, "d"), ::mkType(*type->pool, sp)), ::std::make_pair(ASTPattern(ASTPattern::TagBind(), sp, "idx"), ::mkType(*type->pool, sp))), ::mkType(*type->pool, sp), mv$(nodeMatch), false, false, false);
        ::std::string enumName = type->data.as_Path()->nodes().back().name().c_str();

        auto nodeRev = NEWNODE(CallPath, this->getTraitPathDecoder() + "read_enum_variant", vec$(NEWNODE(NamedValue, ASTPath("d")), NEWNODE(UniOp, ASTExprNodeUniOp::REF, NEWNODE(Array, mv$(varNameStrs))), mv$(nodeVarClosure)));

        auto node = NEWNODE(CallPath, this->getTraitPathDecoder() + "read_enum", vec$(NEWNODE(NamedValue, ASTPath("d")), NEWNODE(String, enumName), this->decClosure(*type->pool, sp, mv$(nodeRev))));

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), mv$(node));
    }
} gDeriveRustcDecodable;

class DeriverConstParamTy: public Deriver {
    ASTImpl handleGeneric(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, ::std::vector<ASTType*> typesToBound) const {
        const ASTPath traitPath = getPath(opts.coreName, "marker", "StructuralPartialEq");
        ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, traitPath, mv$(typesToBound));
        ASTImpl rv(ASTImplDef(mv$(params), makeSpanned(sp, traitPath), type->clone()));
        return mv$(rv);
    }

public:
    const char* traitName() const override {
        return "ConstParamTy";
    }

    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const override {
        return handleGeneric(sp, opts, p, type, this->getFieldBounds(str));
    }

    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const override {
        return handleGeneric(sp, opts, p, type, this->getFieldBounds(enm));
    }
} gDeriveConstParamTy;

class DeriverUnsizedConstParamTy: public Deriver {
    ASTImpl handleGeneric(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, ::std::vector<ASTType*> typesToBound) const {
        const ASTPath traitPath = getPath(opts.coreName, "marker", "UnsizedConstParamTy");
        const ASTPath eqPath = getPath(opts.coreName, "cmp", "Eq");

        ::std::vector<ASTType*> typesToBoundByEq;
        for (const auto* boundedType : typesToBound) {
            typesToBoundByEq.push_back(boundedType->clone());
        }

        ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, traitPath, mv$(typesToBound));
        unsigned int typeIndex = 0;
        for (const auto& param : p.params) {
            if (const auto* typeParam = param.opt_Type()) {
                params.addBound(ASTGenericBound::make_IsTrait({sp, {}, mkType(*type->pool, sp, typeParam->name(), typeIndex), {}, eqPath}));
                typeIndex++;
            }
        }
        for (auto*& boundedType : typesToBoundByEq) {
            params.addBound(ASTGenericBound::make_IsTrait({sp, {}, mv$(boundedType), {}, eqPath}));
        }

        return ASTImpl(ASTImplDef(mv$(params), makeSpanned(sp, traitPath), type->clone()));
    }

public:
    const char* traitName() const override {
        return "UnsizedConstParamTy";
    }

    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const override {
        return handleGeneric(sp, opts, p, type, this->getFieldBounds(str));
    }

    ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const override {
        return handleGeneric(sp, opts, p, type, this->getFieldBounds(enm));
    }
} gDeriveUnsizedConstParamTy;

// --------------------------------------------------------------------
// Select and dispatch the correct derive() handler
// --------------------------------------------------------------------
static const Deriver* findImpl(const RcString& traitName) {
#define _(obj)                        \
    if (traitName == obj.traitName()) \
        return &obj;
    _(gDeriveDebug)
    _(gDerivePartialeq)
    _(gDerivePartialord)
    _(gDeriveEq)
    _(gDeriveOrd)
    _(gDeriveClone)
    _(gDeriveCopy)
    _(gDeriveDefault)
    _(gDeriveHash)
    _(gDeriveRustcEncodable)
    _(gDeriveRustcDecodable)
    _(gDeriveConstParamTy)
    _(gDeriveUnsizedConstParamTy)
#undef _
    return nullptr;
}

static const Deriver* findBuiltinDerive(const ASTPath& traitPath) {
    if (traitPath.isTrivial()) {
        return findImpl(traitPath.asTrivial());
    }
    if (const auto* path = traitPath.cls.opt_Relative()) {
        if (path->nodes.size() >= 2 && (path->nodes.front().name() == "core" || path->nodes.front().name() == "std")) {
            return findImpl(path->nodes.back().name());
        }
    }
    if (const auto* path = traitPath.cls.opt_Absolute()) {
        if (!path->nodes.empty() && (path->crate == "=core" || path->crate == "=std")) {
            return findImpl(path->nodes.back().name());
        }
    }
    return nullptr;
}

namespace {
    std::vector<ASTPath> getDeriveItems(const ASTAttribute& attr) {
        Token tok;
        std::vector<ASTPath> rv;

        TTStream lex(attr.span(), ParseState(), attr.data());
        lex.getTokenCheck(TOK_PAREN_OPEN);
        while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
            if (lex.getTokenIf(TOK_DOUBLE_COLON)) {
                auto item = lex.lookahead(0) == TOK_STRING ? ASTPath(lex.getTokenCheck(TOK_STRING).str().c_str(), {}) : ASTPath((std::string("=") + lex.getTokenCheck(TOK_IDENT).ident().name.c_str()).c_str(), {});
                lex.getTokenCheck(TOK_DOUBLE_COLON);
                do {
                    item += ASTPathNode(lex.getTokenCheck(TOK_IDENT).ident().name);
                } while (lex.getTokenIf(TOK_DOUBLE_COLON));
                rv.push_back(std::move(item));
            } else if (lex.getTokenIf(TOK_INTERPOLATED_TYPE, tok)) {
                const auto& ty = tok.fragType();
                ASSERT_BUG(lex.pointSpan(), ty->isPath(), "TODO: No path :ty in derive, " << ty);
                ASSERT_BUG(lex.pointSpan(), ty->data.as_Path(), "" << ty);
                rv.push_back(*ty->data.as_Path());
            } else {
                auto item = ASTPath::newRelative({}, {});
                do {
                    item += ASTPathNode(lex.getTokenCheck(TOK_IDENT).ident().name);
                } while (lex.getTokenIf(TOK_DOUBLE_COLON));
                rv.push_back(std::move(item));
            }

            if (lex.lookahead(0) != TOK_COMMA) {
                break;
            }
            lex.getTokenCheck(TOK_COMMA);
        }
        lex.getTokenCheck(TOK_PAREN_CLOSE);
        return rv;
    }

    ASTType* makeType(stl::ObjPool& pool, const Span& sp, const ASTAbsolutePath& path, const ASTGenericParams& params) {
        ASTType* type = mkType(pool, sp, path);
        auto& typesArgs = type->path().nodes().back().args();
        for (const auto& param : params.params) {
            if (const auto* pe = param.opt_Type()) {
                typesArgs.entries.push_back(mkType(pool, ASTTypeTags::Arg(), sp, pe->name()));
            }
            if (const auto* pe = param.opt_Value()) {
                auto p = ASTPath(pe->name().name);
                typesArgs.entries.push_back(ASTExprNodeP(new ASTExprNodeNamedValue(std::move(p))));
            }
        }
        return type;
    }

    bool substituteType(ASTType*& type, const RcString& from, ASTType* to);
    bool substitutePath(ASTPath& path, const RcString& from, ASTType* to);

    bool substitutePathParams(ASTPathParams& params, const RcString& from, ASTType* to) {
        bool changed = false;
        for (auto& param : params.entries) {
            TU_MATCH_HDRA((param), {)
            TU_ARMA(Null, e) {}
            TU_ARMA(Lifetime, e) {}
            TU_ARMA(Type, e) { changed |= substituteType(e, from, to); }
            TU_ARMA(Value, e) {}
            TU_ARMA(AssociatedTyEqual, e) {
                changed |= substitutePathParams(e.first.args(), from, to);
                changed |= substituteType(e.second, from, to);
            }
            TU_ARMA(AssociatedTyBound, e) {
                changed |= substitutePathParams(e.first.args(), from, to);
                for (auto& trait : e.second) {
                    changed |= substitutePath(*trait.path, from, to);
                }
            }
            }
        }
        return changed;
    }

    bool substitutePath(ASTPath& path, const RcString& from, ASTType* to) {
        bool changed = false;
        if (!path.cls.is_Local() && !path.cls.is_Invalid()) {
            for (auto& node : path.nodes()) {
                changed |= substitutePathParams(node.args(), from, to);
            }
        }
        if (auto* ufcs = path.cls.opt_UFCS()) {
            changed |= substituteType(ufcs->type, from, to);
            if (ufcs->trait) {
                changed |= substitutePath(*ufcs->trait, from, to);
            }
        }
        return changed;
    }

    bool substituteType(ASTType*& type, const RcString& from, ASTType* to) {
        if (const auto* generic = type->data.opt_Generic()) {
            if (generic->name == from) {
                type = to->clone();
                return true;
            }
            return false;
        }
        if (const auto* path = type->data.opt_Path()) {
            if ((*path)->isTrivial() && (*path)->asTrivial() == from) {
                type = to->clone();
                return true;
            }
        }

        bool changed = false;
        TU_MATCH_HDRA((type->data), {)
        TU_ARMA(None, e) {}
        TU_ARMA(Any, e) {}
        TU_ARMA(Bang, e) {}
        TU_ARMA(Unit, e) {}
        TU_ARMA(Macro, e) {}
        TU_ARMA(Primitive, e) {}
        TU_ARMA(Function, e) {
            changed |= substituteType(e.info.rettype, from, to);
            for (auto*& arg : e.info.argTypes) {
                changed |= substituteType(arg, from, to);
            }
        }
        TU_ARMA(Tuple, e) {
            for (auto*& inner : e.innerTypes) {
                changed |= substituteType(inner, from, to);
            }
        }
        TU_ARMA(Borrow, e) { changed |= substituteType(e.inner, from, to); }
        TU_ARMA(Pointer, e) { changed |= substituteType(e.inner, from, to); }
        TU_ARMA(Array, e) { changed |= substituteType(e.inner, from, to); }
        TU_ARMA(Slice, e) { changed |= substituteType(e.inner, from, to); }
        TU_ARMA(Pattern, e) { changed |= substituteType(e.inner, from, to); }
        TU_ARMA(Generic, e) {}
        TU_ARMA(Path, e) { changed |= substitutePath(*e, from, to); }
        TU_ARMA(TraitObject, e) {
            for (auto& trait : e.traits) {
                changed |= substitutePath(*trait.path, from, to);
            }
        }
        TU_ARMA(ErasedType, e) {
            for (auto& trait : e->traits) {
                changed |= substitutePath(*trait.path, from, to);
            }
            for (auto& trait : e->maybeTraits) {
                changed |= substitutePath(*trait.path, from, to);
            }
            if (e->use) {
                changed |= substitutePathParams(*e->use, from, to);
            }
        }
        }
        return changed;
    }

    bool substituteBound(ASTGenericBound& bound, const RcString& from, ASTType* to) {
        bool changed = false;
        TU_MATCH_HDRA((bound), {)
        TU_ARMA(None, e) {}
        TU_ARMA(Lifetime, e) {}
        TU_ARMA(TypeLifetime, e) { changed |= substituteType(e.type, from, to); }
        TU_ARMA(IsTrait, e) {
            changed |= substituteType(e.type, from, to);
            changed |= substitutePath(e.trait, from, to);
        }
        TU_ARMA(MaybeTrait, e) {
            changed |= substituteType(e.type, from, to);
            changed |= substitutePath(e.trait, from, to);
        }
        TU_ARMA(NotTrait, e) {
            changed |= substituteType(e.type, from, to);
            changed |= substitutePath(e.trait, from, to);
        }
        TU_ARMA(Equality, e) {
            changed |= substituteType(e.type, from, to);
            changed |= substituteType(e.replacement, from, to);
        }
        }
        return changed;
    }

    ASTGenericParams makeImplParams(stl::ObjPool& pool, const Span& sp, const ASTGenericParams& source) {
        auto params = source.clone();
        for (auto& param : params.params) {
            if (auto* type = param.opt_Type()) {
                type->getDefault() = mkType(pool, sp);
            } else if (auto* value = param.opt_Value()) {
                value->defaultValue() = ASTExpr();
            }
        }
        return params;
    }

    bool isCoercePointee(const ASTPath& traitPath) {
        if (traitPath.isTrivial()) {
            return traitPath.asTrivial() == "CoercePointee";
        }
        if (const auto* path = traitPath.cls.opt_Relative()) {
            return path->nodes.size() == 3
                && (path->nodes[0].name() == "core" || path->nodes[0].name() == "std")
                && path->nodes[1].name() == "marker"
                && path->nodes[2].name() == "CoercePointee";
        }
        if (const auto* path = traitPath.cls.opt_Absolute()) {
            return (path->crate == "=core" || path->crate == "=std")
                && path->nodes.size() == 2
                && path->nodes[0].name() == "marker"
                && path->nodes[1].name() == "CoercePointee";
        }
        return false;
    }

    void addCoercePointeeImpl(const Span& sp, ASTModule& mod, ASTPath traitPath, ASTGenericParams params, ASTType* selfType) {
        mod.addItem(sp, ASTVisibility::makeBarePrivate(), "", ASTImpl(ASTImplDef(mv$(params), makeSpanned(sp, mv$(traitPath)), selfType)), {});
    }

    void deriveCoercePointee(const Span& sp, const DeriveOpts& opts, ASTModule& mod, const ASTGenericParams& sourceParams, ASTType* selfType, const ASTStruct& str) {
        bool hasField = false;
        TU_MATCH_HDRA((str.data), {)
        TU_ARMA(Unit, e) {}
        TU_ARMA(Struct, e) { hasField = !e.ents.empty(); }
        TU_ARMA(Tuple, e) { hasField = !e.ents.empty(); }
        }
        if (!hasField) {
            ERROR(sp, E0000, "CoercePointee can only be derived for a struct with fields");
        }

        size_t pointeeIndex = SIZE_MAX;
        size_t typeCount = 0;
        for (size_t i = 0; i < sourceParams.params.size(); i++) {
            if (const auto* type = sourceParams.params[i].opt_Type()) {
                typeCount++;
                if (type->attrs().has("pointee")) {
                    if (pointeeIndex != SIZE_MAX) {
                        ERROR(sp, E0000, "Only one CoercePointee type parameter can have #[pointee]");
                    }
                    pointeeIndex = i;
                }
            }
        }
        if (typeCount == 0) {
            ERROR(sp, E0000, "CoercePointee requires a generic type parameter");
        }
        if (typeCount == 1 && pointeeIndex == SIZE_MAX) {
            for (size_t i = 0; i < sourceParams.params.size(); i++) {
                if (sourceParams.params[i].is_Type()) {
                    pointeeIndex = i;
                    break;
                }
            }
        }
        if (pointeeIndex == SIZE_MAX) {
            ERROR(sp, E0000, "One CoercePointee type parameter must have #[pointee]");
        }

        const auto& pointeeName = sourceParams.params[pointeeIndex].as_Type().name();
        const auto targetName = RcString::newInterned("__S");
        auto* targetParamType = mkType(*selfType->pool, sp, targetName);
        auto* targetSelfType = selfType->clone();
        ASSERT_BUG(sp, substituteType(targetSelfType, pointeeName, targetParamType), "CoercePointee self type does not use " << pointeeName);

        addCoercePointeeImpl(sp, mod, getPath(opts.coreName, "marker", "CoercePointeeValidated"), makeImplParams(*selfType->pool, sp, sourceParams), selfType->clone());

        auto params = makeImplParams(*selfType->pool, sp, sourceParams);
        for (const auto& sourceBound : sourceParams.bounds) {
            auto targetBound = sourceBound.clone();
            if (substituteBound(targetBound, pointeeName, targetParamType)) {
                params.bounds.push_back(mv$(targetBound));
            }
        }
        params.addTyParam(ASTTypeParam(*selfType->pool, sp, ASTAttributeList(), targetName));

        auto unsizePath = getPath(opts.coreName, "marker", "Unsize");
        unsizePath.nodes().back().args().entries.push_back(targetParamType->clone());
        params.addBound(ASTGenericBound::make_IsTrait({sp, {}, mkType(*selfType->pool, sp, pointeeName), {}, mv$(unsizePath)}));

        auto dispatchPath = getPath(opts.coreName, "ops", "DispatchFromDyn");
        dispatchPath.nodes().back().args().entries.push_back(targetSelfType->clone());
        addCoercePointeeImpl(sp, mod, mv$(dispatchPath), params.clone(), selfType->clone());

        auto coercePath = getPath(opts.coreName, "ops", "CoerceUnsized");
        coercePath.nodes().back().args().entries.push_back(targetSelfType);
        addCoercePointeeImpl(sp, mod, mv$(coercePath), mv$(params), selfType->clone());
    }

    template <typename T>
    void deriveCoercePointee(const Span& sp, const DeriveOpts&, ASTModule&, const ASTGenericParams&, ASTType*, const T&) {
        ERROR(sp, E0000, "CoercePointee can only be derived for structs");
    }

    std::vector<RcString> findMacro(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const ASTModule& mod, const ASTPath& traitPath) {
        std::vector<RcString> macPath;

        if (traitPath.isTrivial()) {
            auto macName = traitPath.asTrivial();

            for (const auto& macImport : mod.macroImports) {
                if (macImport.name == macName) {
                    TU_MATCH_HDRA( (macImport.ref), {)
                    default:
                        break;
                        TU_ARMA(ExternalProcMacro, pm) {
                            DEBUG("proc_macro " << pm->path);
                            macPath.push_back(pm->path.crateName());
                            macPath.insert(macPath.end(), pm->path.components().begin(), pm->path.components().end());
                        }
                    }
                    if( !macPath.empty() ) {
                        break;
                    }
                }
            }
        }
        if (macPath.empty()) {
            auto mac = ExpandLookupMacro(sp, wb, crate, LList<const ASTModule*>(nullptr, &mod), traitPath);

            TU_MATCH_HDRA( (mac), {)
            TU_ARMA(None, e) {
                    // Leave `mac_path` empty, triggering an error in caller
                }
                TU_ARMA(ExternalProcMacro, extProcMac) {
                    macPath.push_back(extProcMac->path.crateName());
                    macPath.insert(macPath.end(), extProcMac->path.components().begin(), extProcMac->path.components().end());
                }
                TU_ARMA(BuiltinProcMacro, procMac) {
                    TODO(sp, "Handle builtin proc macro");
                }
                TU_ARMA(MacroRules, mrPtr) {
                    TODO(sp, "Custom derive using macro_rules?");
                }
            }
        }
        return macPath;
    }
}

template <typename T>
static void deriveItem(const Span& sp, const WireBoard& wb, const ASTCrate& crate, ASTModule& mod, const ASTAttribute& attr, const ASTAbsolutePath& path, slice<const ASTAttribute> attrs, const ASTVisibility& vis, const T& item) {
    auto deriveItems = getDeriveItems(attr);
    if (deriveItems.empty()) {
        return;
    }

    DEBUG("path = " << path);

    auto type = makeType(*crate.pool, sp, path, item.params());

    DeriveOpts opts = {crate.extCratenameCore};
    // `#[derive(Copy)]` changes what a derived `Clone` means, and it may be
    // written in a separate attribute from the `Clone`.
    for (const auto& a : attrs) {
        if (a.name() != "derive") {
            continue;
        }
        for (const auto& derived : getDeriveItems(a)) {
            if (!derived.nodes().empty() && derived.nodes().back().name() == "Copy") {
                opts.derivesCopy = true;
            }
        }
    }

    ::std::vector<ASTPath> missingHandlers;
    for (const auto& traitPath : deriveItems) {
        DEBUG("- " << traitPath);

        if (isCoercePointee(traitPath)) {
            deriveCoercePointee(sp, opts, mod, item.params(), type, item);
            continue;
        }

        if (auto dp = findBuiltinDerive(traitPath)) {
            mod.addItem(sp, ASTVisibility::makeBarePrivate(), "", dp->handleItem(sp, opts, item.params(), type, item), {});
            continue;
        }

        std::vector<RcString> macPath = findMacro(sp, wb, crate, mod, traitPath);
        if (!macPath.empty()) {
            auto lex = ProcMacroInvoke(sp, wb, crate, macPath, attrs, vis, path.nodes.back(), item);
            if (lex) {
                lex->parseState().module = &mod;
                ParseModRootItems(*lex, mod);
            } else {
                ERROR(sp, E0000, "proc_macro derive failed");
            }
            continue;
        }

        DEBUG("> No handler for " << traitPath);
        missingHandlers.push_back(traitPath);
    }

    if (!missingHandlers.empty()) {
        ERROR(sp, E0000, "Failed to apply #[derive] - Missing handlers for " << missingHandlers);
    }
}

class DecoratorDerive: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    // A derive macro must see every attribute on the item (e.g. a `#[repr(C)]` written
    // above the derive, which bytemuck's `Pod` checks for); the input serialiser strips
    // `derive` attributes themselves.
    bool wantsAllAttrs() const override {
        return true;
    }

    void handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t modIdx, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        TU_MATCH_DEF(
            ASTItem,
            (i),
            (e),
            (TODO(sp, "Handle #[derive] for other item types - " << i.tagStr());),
            (
                None,
                // Ignore, it's been deleted
            ),
            (Union, deriveItem(sp, wb, crate, mod, attr, path, attrs, vis, e);),
            (Enum, deriveItem(sp, wb, crate, mod, attr, path, attrs, vis, e);),
            (Struct, deriveItem(sp, wb, crate, mod, attr, path, attrs, vis, e);)
        )
    }
};

STATIC_DECORATOR("derive", DecoratorDerive)

// TODO: `derive_const` should generate const impls, but trustme doesn't care
class DecoratorDeriveConst: public DecoratorDerive {};
STATIC_DECORATOR("derive_const", DecoratorDeriveConst)

class CDocHandler: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTStructItem& si) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTTupleItem& si) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTEnumVariant& ev) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeMatchArm& expr) const override {
    }
};

STATIC_DECORATOR("doc", CDocHandler);

enum eItemType {
    ITEM_TRAIT,
    ITEM_STRUCT,
    ITEM_ENUM,
    ITEM_UNION,
    ITEM_FN,
    ITEM_EXTERN_FN,
    ITEM_STATIC,
    ITEM_TYPE_ALIAS,
};

struct Handler {
    typedef void (*cbT)(const Span& sp, ASTCrate& crate, const std::string&, const ASTAbsolutePath&);
    eItemType type;
    cbT cb;

    Handler(eItemType type, cbT cb)
        : type(type)
        , cb(cb)
    {
    }
};

struct StrcmpTy {
    bool operator()(const char* a, const char* b) const {
        return std::strcmp(a, b) < 0;
    }
};

static std::map<const char*, Handler, StrcmpTy> gHandlers;

void handleSave(const Span& sp, ASTCrate& crate, const std::string& name, const ASTAbsolutePath& path) {
    auto rv = crate.langItems.insert(::std::make_pair(name, path));
    if (!rv.second) {
        const auto& otherPath = rv.first->second;
        if (path != otherPath) {
            // HACK: Anon modules get visited twice, so can lead to duplicate annotations
            ERROR(sp, E0000, "Duplicate definition of language item '" << name << "' - " << otherPath << " and " << path);
        }
    } else {
        DEBUG("Bind '" << name << "' to " << path);
    }
}

void handleLangItem(const Span& sp, ASTCrate& crate, const ASTAbsolutePath& path, const ::std::string& name, eItemType type, ASTItem& item) {
    if (gHandlers.empty()) {
        struct H {
            static void add(const char* n, Handler h) {
                gHandlers.insert(std::make_pair(n, std::move(h)));
            }
        };

        H::add("phantom_fn", Handler(ITEM_FN, handleSave));
        H::add("send", Handler(ITEM_TRAIT, handleSave));
        H::add("sync", Handler(ITEM_TRAIT, handleSave));
        H::add("sized", Handler(ITEM_TRAIT, handleSave));
        H::add("copy", Handler(ITEM_TRAIT, handleSave));
        {
            H::add("clone", Handler(ITEM_TRAIT, handleSave));
        }
        // ops traits
        H::add("drop", Handler(ITEM_TRAIT, handleSave));
        H::add("add", Handler(ITEM_TRAIT, handleSave));
        H::add("sub", Handler(ITEM_TRAIT, handleSave));
        H::add("mul", Handler(ITEM_TRAIT, handleSave));
        H::add("div", Handler(ITEM_TRAIT, handleSave));
        H::add("rem", Handler(ITEM_TRAIT, handleSave));

        H::add("neg", Handler(ITEM_TRAIT, handleSave));
        H::add("not", Handler(ITEM_TRAIT, handleSave));

        H::add("bitand", Handler(ITEM_TRAIT, handleSave));
        H::add("bitor", Handler(ITEM_TRAIT, handleSave));
        H::add("bitxor", Handler(ITEM_TRAIT, handleSave));
        H::add("shl", Handler(ITEM_TRAIT, handleSave));
        H::add("shr", Handler(ITEM_TRAIT, handleSave));

        H::add("add_assign", Handler(ITEM_TRAIT, handleSave));
        H::add("sub_assign", Handler(ITEM_TRAIT, handleSave));
        H::add("div_assign", Handler(ITEM_TRAIT, handleSave));
        H::add("rem_assign", Handler(ITEM_TRAIT, handleSave));
        H::add("mul_assign", Handler(ITEM_TRAIT, handleSave));
        H::add("bitand_assign", Handler(ITEM_TRAIT, handleSave));
        H::add("bitor_assign", Handler(ITEM_TRAIT, handleSave));
        H::add("bitxor_assign", Handler(ITEM_TRAIT, handleSave));
        H::add("shl_assign", Handler(ITEM_TRAIT, handleSave));
        H::add("shr_assign", Handler(ITEM_TRAIT, handleSave));

        H::add("index", Handler(ITEM_TRAIT, handleSave));
        H::add("deref", Handler(ITEM_TRAIT, handleSave));
        H::add("index_mut", Handler(ITEM_TRAIT, handleSave));
        H::add("deref_mut", Handler(ITEM_TRAIT, handleSave));
        H::add("fn", Handler(ITEM_TRAIT, handleSave));
        H::add("fn_mut", Handler(ITEM_TRAIT, handleSave));
        H::add("fn_once", Handler(ITEM_TRAIT, handleSave));

        H::add("eq", Handler(ITEM_TRAIT, handleSave));
        H::add("ord", Handler(ITEM_TRAIT, handleSave)); // In 1.29 this is Ord, before it was PartialOrd
        {
            H::add("partial_ord", Handler(ITEM_TRAIT, handleSave)); // New name for v1.29
        }

        H::add("unsize", Handler(ITEM_TRAIT, handleSave));
        H::add("coerce_unsized", Handler(ITEM_TRAIT, handleSave));
        H::add("freeze", Handler(ITEM_TRAIT, handleSave)); // TODO: What version?

        H::add("iterator", Handler(ITEM_TRAIT, handleSave));    /* trustme just desugars? */
        H::add("debug_trait", Handler(ITEM_TRAIT, handleSave)); /* TODO: Poke derive() with this */

        {
            H::add("termination", Handler(ITEM_TRAIT, handleSave)); // 1.29 - trait used for non-() main
        }

        {
            H::add("pointee_trait", Handler(ITEM_TRAIT, handleSave));     // 1.54 - pointer metadata trait
            H::add("dyn_metadata", Handler(ITEM_STRUCT, handleSave));     // 1.54 - `dyn Trait` metadata structure
            H::add("structural_peq", Handler(ITEM_TRAIT, handleSave));    // 1.54 - Structural equality trait (partial)
            H::add("structural_teq", Handler(ITEM_TRAIT, handleSave));    // 1.54 - Structural equality trait (total)
            H::add("discriminant_kind", Handler(ITEM_TRAIT, handleSave)); // 1.54 - trait: used for the `discriminant_kind` intrinsic
        }

        H::add("non_zero", Handler(ITEM_STRUCT, handleSave));
        H::add("phantom_data", Handler(ITEM_STRUCT, handleSave));
        H::add("unsafe_cell", Handler(ITEM_STRUCT, handleSave));

        {
            H::add("RangeFull", Handler(ITEM_STRUCT, [](const auto& sp, auto& crate, const auto&, const auto& p) {
                handleSave(sp, crate, "range_full", p);
            }));
            H::add("Range", Handler(ITEM_STRUCT, [](const auto& sp, auto& crate, const auto&, const auto& p) {
                handleSave(sp, crate, "range", p);
            }));
            H::add("RangeFrom", Handler(ITEM_STRUCT, [](const auto& sp, auto& crate, const auto&, const auto& p) {
                handleSave(sp, crate, "range_from", p);
            }));
            H::add("RangeTo", Handler(ITEM_STRUCT, [](const auto& sp, auto& crate, const auto&, const auto& p) {
                handleSave(sp, crate, "range_to", p);
            }));
            H::add("RangeInclusive", Handler(ITEM_STRUCT, [](const auto& sp, auto& crate, const auto&, const auto& p) {
                handleSave(sp, crate, "range_inclusive", p);
            }));
            H::add("RangeToInclusive", Handler(ITEM_STRUCT, [](const auto& sp, auto& crate, const auto&, const auto& p) {
                handleSave(sp, crate, "range_to_inclusive", p);
            }));
        }

        {
            H::add("unwind_safe", Handler(ITEM_TRAIT, handleSave));     // 1.54 - UnwindSafe trait
            H::add("ref_unwind_safe", Handler(ITEM_TRAIT, handleSave)); // 1.54 - RefUnwindSafe trait
        }
        {
            H::add("transmute_trait", Handler(ITEM_TRAIT, handleSave)); // 1.74 - `BikeshedIntrinsicFrom` trait
            // - Markers
            H::add("destruct", Handler(ITEM_TRAIT, handleSave));       // 1.74 - `Destruct` trait
            H::add("tuple_trait", Handler(ITEM_TRAIT, handleSave));    // 1.74 - `Tuple` trait (must be implemented for all tuples)
            H::add("pointer_like", Handler(ITEM_TRAIT, handleSave));   // 1.74 - `PointerLike` trait
            H::add("const_param_ty", Handler(ITEM_TRAIT, handleSave)); // 1.74 - `ConstParamTy` trait
            H::add("fn_ptr_trait", Handler(ITEM_TRAIT, handleSave));   // 1.74 - `FnPtr` trait

            // Structs
            H::add("transmute_opts", Handler(ITEM_STRUCT, handleSave)); // 1.74 - `Assume` struct
            H::add("ptr_unique", Handler(ITEM_STRUCT, handleSave));     // 1.74 - `::core::ptr::Unique`
            H::add("CStr", Handler(ITEM_STRUCT, handleSave));           // 1.74 - `::core::ffi::CStr` - Why? (miri?)
            H::add("String", Handler(ITEM_STRUCT, handleSave));         // 1.74 - `::alloc::string::String` - Why? (miri?)

            H::add("from_yeet", Handler(ITEM_FN, handleSave));                            // 1.74 - `::core::try_trait::from_yeet`
            H::add("panic_nounwind", Handler(ITEM_FN, handleSave));                       // 1.74 - `::core::panicking::panic`
            H::add("panic_display", Handler(ITEM_FN, handleSave));                        // 1.74 - `::core::panicking::panic_display`
            H::add("panic_bounds_check", Handler(ITEM_FN, handleSave));                   // 1.74 - `::core::panicking::panic_bounds_check`
            H::add("panic_misaligned_pointer_dereference", Handler(ITEM_FN, handleSave)); // 1.74 - `::core::panicking::panic_misaligned_pointer_dereference`
            H::add("panic_cannot_unwind", Handler(ITEM_FN, handleSave));                  // 1.74 - `::core::panicking::panic_cannot_unwind`
            H::add("panic_in_cleanup", Handler(ITEM_FN, handleSave));                     // 1.74 - `::core::panicking::panic_in_cleanup `
            H::add("const_panic_fmt", Handler(ITEM_FN, handleSave));                      // 1.74 - `::core::panicking::const_panic_fmt`

            // Enums
            H::add("c_void", Handler(ITEM_ENUM, handleSave)); // 1.74 - `::core::ffi::c_void` - Why? (miri?)
            H::add("Option", Handler(ITEM_ENUM, handleSave)); // 1.74 - `::core::option::Option`

            // - Formatting
            H::add("format_arguments", Handler(ITEM_STRUCT, handleSave));   // 1.74 - `::core::fmt::Arguments`
            H::add("format_placeholder", Handler(ITEM_STRUCT, handleSave)); // 1.74 - `::core::fmt::rt::Placeholder`
            H::add("format_argument", Handler(ITEM_STRUCT, handleSave));    // 1.74 - `::core::fmt::rt::Argument`
            H::add("format_unsafe_arg", Handler(ITEM_STRUCT, handleSave));  // 1.74 - `::core::fmt::rt::UnsafeArg`
            H::add("format_alignment", Handler(ITEM_ENUM, handleSave));     // 1.74 - `::core::fmt::rt::Alignment`
            H::add("format_count", Handler(ITEM_ENUM, handleSave));         // 1.74 - `::core::fmt::rt::Count`

            // - Futures
            H::add("ResumeTy", Handler(ITEM_STRUCT, handleSave)); // 1.74 - `::core::future::ResumeTy`
            H::add("Poll", Handler(ITEM_ENUM, handleSave));       // 1.74 - `::core::task::poll::Poll`
            H::add("Context", Handler(ITEM_STRUCT, handleSave));  // 1.74 - `::core::task::wake::Context`
        }
        {
            H::add("contract_build_check_ensures", Handler(ITEM_FN, handleSave)); // 1.90 - `::core::contracts::build_check_ensures`
            H::add("contract_check_requires", Handler(ITEM_FN, handleSave));      // 1.90 - `::core::intrinsics::contract_check_requires`
            H::add("contract_check_ensures", Handler(ITEM_FN, handleSave));       // 1.90 - `::core::intrinsics::contract_check_ensures`
            H::add("use_cloned", Handler(ITEM_TRAIT, handleSave));                // 1.90 - `::core::clone::use_cloned` - for the `.use` syntax

            H::add("Ordering", Handler(ITEM_ENUM, handleSave)); // comparison ordering

            H::add("meta_sized", Handler(ITEM_TRAIT, handleSave));                  // ::core::marker::MetaSized
            H::add("pointee_sized", Handler(ITEM_TRAIT, handleSave));               // ::core::marker::PointeeSized
            H::add("bikeshed_guaranteed_no_drop", Handler(ITEM_TRAIT, handleSave)); // ::core::marker::BikeshedGuaranteedNoDrop
            H::add("unsafe_unpin", Handler(ITEM_TRAIT, handleSave));                // ::core::marker::UnsafeUnpin
            H::add("unsized_const_param_ty", Handler(ITEM_TRAIT, handleSave));      // ::core::marker::UnsizedConstParamTy
            H::add("coerce_pointee_validated", Handler(ITEM_TRAIT, handleSave));    // ::core::marker::CoercePointeeValidated

            H::add("async_fn", Handler(ITEM_TRAIT, handleSave));
            H::add("async_fn_mut", Handler(ITEM_TRAIT, handleSave));
            H::add("async_fn_once", Handler(ITEM_TRAIT, handleSave));

            H::add("async_fn_kind_helper", Handler(ITEM_TRAIT, handleSave)); // ::core::ops::async_function::internal_implementation_detail::AsyncFnKindHelper
            H::add("coroutine_state", Handler(ITEM_ENUM, handleSave));       // ::core::ops::coroutine::CoroutineState
            H::add("coroutine", Handler(ITEM_TRAIT, handleSave));            // ::core::ops::coroutine::Coroutine
            H::add("deref_pure", Handler(ITEM_TRAIT, handleSave));           // ::core::ops::deref::DerefPure
            H::add("legacy_receiver", Handler(ITEM_TRAIT, handleSave));      // ::core::ops::deref::LegacyReceiver

            H::add("type_id", Handler(ITEM_STRUCT, handleSave)); // ::core::any::TypeId

            H::add("async_iterator", Handler(ITEM_TRAIT, handleSave)); // ::core::async_iter::async_iter::AsyncIterator
            H::add("fused_iterator", Handler(ITEM_TRAIT, handleSave)); // ::core::iter::traits::marker::FusedIterator

            // Various panic handlers
            H::add("panic_const_add_overflow", Handler(ITEM_FN, handleSave));
            H::add("panic_const_sub_overflow", Handler(ITEM_FN, handleSave));
            H::add("panic_const_mul_overflow", Handler(ITEM_FN, handleSave));
            H::add("panic_const_div_overflow", Handler(ITEM_FN, handleSave));
            H::add("panic_const_rem_overflow", Handler(ITEM_FN, handleSave));
            H::add("panic_const_neg_overflow", Handler(ITEM_FN, handleSave));
            H::add("panic_const_shr_overflow", Handler(ITEM_FN, handleSave));
            H::add("panic_const_shl_overflow", Handler(ITEM_FN, handleSave));
            H::add("panic_const_div_by_zero", Handler(ITEM_FN, handleSave));
            H::add("panic_const_rem_by_zero", Handler(ITEM_FN, handleSave));
            H::add("panic_const_coroutine_resumed", Handler(ITEM_FN, handleSave));
            H::add("panic_const_async_fn_resumed", Handler(ITEM_FN, handleSave));
            H::add("panic_const_async_gen_fn_resumed", Handler(ITEM_FN, handleSave));
            H::add("panic_const_gen_fn_none", Handler(ITEM_FN, handleSave));
            H::add("panic_const_coroutine_resumed_panic", Handler(ITEM_FN, handleSave));
            H::add("panic_const_async_fn_resumed_panic", Handler(ITEM_FN, handleSave));
            H::add("panic_const_async_gen_fn_resumed_panic", Handler(ITEM_FN, handleSave));
            H::add("panic_const_gen_fn_none_panic", Handler(ITEM_FN, handleSave));

            H::add("panic_const_coroutine_resumed_drop", Handler(ITEM_FN, handleSave));
            H::add("panic_const_async_fn_resumed_drop", Handler(ITEM_FN, handleSave));
            H::add("panic_const_async_gen_fn_resumed_drop", Handler(ITEM_FN, handleSave));
            H::add("panic_const_gen_fn_none_drop", Handler(ITEM_FN, handleSave));

            H::add("panic_null_pointer_dereference", Handler(ITEM_FN, handleSave));
            H::add("panic_invalid_enum_construction", Handler(ITEM_FN, handleSave));

            H::add("unsafe_pinned", Handler(ITEM_STRUCT, handleSave)); // ::core::pin::unsafe_pinned::UnsafePinned

            H::add("RangeCopy", Handler(ITEM_STRUCT, handleSave));          // ::core::range::Range
            H::add("RangeInclusiveCopy", Handler(ITEM_STRUCT, handleSave)); // ::core::range::RangeInclusive
            H::add("RangeFromCopy", Handler(ITEM_STRUCT, handleSave));      // ::core::range::RangeFrom

            H::add("async_drop", Handler(ITEM_TRAIT, handleSave));       // ::core::future::async_drop::AsyncDrop
            H::add("async_drop_in_place", Handler(ITEM_FN, handleSave)); // ::core::future::async_drop::async_drop_in_place

            H::add("global_alloc_ty", Handler(ITEM_STRUCT, handleSave)); // ::alloc::alloc::Global
        }
    }
    const char* realName = nullptr; // For when lang items have their name changed
    auto it = gHandlers.find(name.c_str());
    if (it != gHandlers.end()) {
        if (type != it->second.type) {
            ERROR(sp, E0000, "Language item '" << name << "' " << path << " - on incorrect item type " << type << " != " << it->second.type);
        }
        it->second.cb(sp, crate, name, path);
        return;
    }

    // Structs
    else if (name == "alloc_layout") {
    } else if (name == "panic_info") {
    } // Struct
    else if (name == "panic_location") {
    } // Struct
    else if (name == "manually_drop") {
    } // Struct

    else if (name == "arc") {
    } // Struct
    else if (name == "rc") {
    } // Struct

    else if (name == "maybe_uninit") {
    } // Union

    // Futures
    else if (name == "unpin") {
    } // Trait
    else if (name == "pin") {
    } // Struct
    else if (name == "future_trait") {
    } // Trait
    else if (name == "from_generator") {
    } // Function
    else if (name == "get_context") {
    } // Function

    // Variable argument lists
    else if (name == "va_list") {
    } // Struct

    // Arbitary receivers
    else if (name == "receiver") {
    } // Trait
    else if (name == "dispatch_from_dyn") {
    } // Trait

    // Generators
    else if (name == "generator") {
    } // - Trait
    else if (name == "generator_state") {
    } // - State enum

    // Try
    else if (name == "Try") {
        realName = "try";
    }

    // Statics
    else if (name == "msvc_try_filter") {
    }

    // Extern functions
    else if (name == "panic_impl") {
    } else if (name == "oom") {
    }

    // Functions
    else if (name == "panic") {
    } else if (name == "panic_bounds_check") {
    } else if (name == "panic_fmt") {
        item.as_Function().markings.linkName = "rust_begin_unwind";
    } else if (name == "str_eq") {
    } else if (name == "drop_in_place") {
    } else if (name == "align_offset") {
    } else if (name == "begin_panic") {
    } // Function
    else if (name == "panic_str") {
    }
    // - builtin `box` support
    else if (name == "exchange_malloc") {
    } else if (name == "exchange_free") {
    } else if (name == "box_free") {
    } else if (name == "owned_box") {
    }
    // - start
    else if (name == "start") {
    }

    else if (name == "eh_personality") {
    }
    // libcompiler_builtins
    // - i128/u128 helpers (not used by trustme)
    else if (name == "i128_add") {
    } else if (name == "i128_addo") {
    } else if (name == "u128_add") {
    } else if (name == "u128_addo") {
    } else if (name == "i128_sub") {
    } else if (name == "i128_subo") {
    } else if (name == "u128_sub") {
    } else if (name == "u128_subo") {
    } else if (name == "i128_mul") {
    } else if (name == "i128_mulo") {
    } else if (name == "u128_mul") {
    } else if (name == "u128_mulo") {
    } else if (name == "i128_div") {
    } else if (name == "i128_rem") {
    } else if (name == "u128_div") {
    } else if (name == "u128_rem") {
    } else if (name == "i128_shl") {
    } else if (name == "i128_shlo") {
    } else if (name == "u128_shl") {
    } else if (name == "u128_shlo") {
    } else if (name == "i128_shr") {
    } else if (name == "i128_shro") {
    } else if (name == "u128_shr") {
    } else if (name == "u128_shro") {
    }

    else {
        ERROR(sp, E0000, "Unknown language item '" << name << "'");
    }

    if (type == ITEM_EXTERN_FN) {
        // TODO: This should force a specific link name instead
        return;
    }

    auto rv = crate.langItems.insert(::std::make_pair(realName == nullptr ? name : realName, path));
    if (!rv.second) {
        const auto& otherPath = rv.first->second;
        if (path != otherPath) {
            // HACK: Anon modules get visited twice, so can lead to duplicate annotations
            ERROR(sp, E0000, "Duplicate definition of language item '" << name << "' - " << otherPath << " and " << path);
        }
    }
}

class DecoratorLangItem: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        auto name = attr.parseEqualsString(wb, crate, mod);
        TU_MATCH_HDRA( (i), {)
        default:
            TODO(sp, "Unknown item type " << i.tagStr() << " with #["<<attr<<"] attached at " << path);
            break;
            TU_ARMA(None, e) {
                // NOTE: Can happen when #[cfg] removed this
            }
            TU_ARMA(Impl, e) {
                if (name == "i8") {
                } else if (name == "u8") {
                } else if (name == "i16") {
                } else if (name == "u16") {
                } else if (name == "i32") {
                } else if (name == "u32") {
                } else if (name == "i64") {
                } else if (name == "u64") {
                } else if (name == "i128") {
                } else if (name == "u128") {
                } else if (name == "isize") {
                } else if (name == "usize") {
                } else if (name == "const_ptr") {
                } else if (name == "mut_ptr") {
                } else if (name == "const_slice_ptr") {
                } else if (name == "mut_slice_ptr") {
                } else if (name == "array") {
                } else if (name == "bool") {
                }
                // rustc_unicode
                else if (name == "char") {
                }
                // collections
                else if (name == "str") {
                } else if (name == "slice") {
                } else if (name == "slice_u8") {
                } // libcore now, `impl [u8]`
                else if (name == "slice_alloc") {
                } // liballoc's impls on [T]
                else if (name == "slice_u8_alloc") {
                } // liballoc's impls on [u8]
                else if (name == "str_alloc") {
                } // liballoc's impls on str
                // std - interestingly
                else if (name == "f32") {
                } else if (name == "f64") {
                } else if (name == "f32_runtime") {
                } else if (name == "f64_runtime") {
                } else {
                    ERROR(sp, E0000, "Unknown lang item '" << name << "' on impl");
                }

                // TODO: Somehow annotate these impls to allow them to provide inherents?
                // - trustme is lazy and inefficient, so these don't matter :)
            }
            TU_ARMA(Function, e) {
                if (e.code().isValid()) {
                    handleLangItem(sp, crate, path, name, ITEM_FN, i);
                } else {
                    handleLangItem(sp, crate, path, name, ITEM_EXTERN_FN, i);
                }
            }
            TU_ARMA(Type, e) {
                handleLangItem(sp, crate, path, name, ITEM_TYPE_ALIAS, i);
            }
            TU_ARMA(Static, e) {
                handleLangItem(sp, crate, path, name, ITEM_STATIC, i);
            }
            TU_ARMA(Struct, e) {
                handleLangItem(sp, crate, path, name, ITEM_STRUCT, i);
            }
            TU_ARMA(Enum, e) {
                handleLangItem(sp, crate, path, name, ITEM_ENUM, i);
            }
            TU_ARMA(Union, e) {
                handleLangItem(sp, crate, path, name, ITEM_UNION, i);
            }
            TU_ARMA(Trait, e) {
                handleLangItem(sp, crate, path, name, ITEM_TRAIT, i);
            }
        }
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const override {
        // TODO: Trait ATYs (a sub-item of others)
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTEnumVariant& ev) const override {
        // TODO: Enum variants (sub-item of other lang items)
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        // TODO: lang items on associated items (e.g. functions - `RangeFull::new`)
    }
};

class DecoratorMain: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        if (i.is_None()) {
            // Ignore.
        } else if (/*const auto* e =*/i.opt_Function()) {
            auto rv = crate.langItems.insert(::std::make_pair(::std::string("trustme-main"), path));
            if (!rv.second) {
                const auto& otherPath = rv.first->second;
                ERROR(sp, E0000, "Duplicate definition of #[main] - " << otherPath << " and " << path);
            }
        } else {
            ERROR(sp, E0000, "#[main] on non-function " << path);
        }
    }
};

class DecoratorStart: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        if (i.is_None()) {
        } else if (i.is_Function()) {
            auto rv = crate.langItems.insert(::std::make_pair(::std::string("trustme-start"), path));
            if (!rv.second) {
                const auto& otherPath = rv.first->second;
                ERROR(sp, E0000, "Duplicate definition of #[start] - " << otherPath << " and " << path);
            }
        } else {
            ERROR(sp, E0000, "#[start] on non-function " << path);
        }
    }
};

class DecoratorPanicImplementation: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        if (i.is_Function()) {
            auto rv = crate.langItems.insert(::std::make_pair(::std::string("trustme-panic_implementation"), path));
            if (!rv.second) {
                const auto& otherPath = rv.first->second;
                ERROR(sp, E0000, "Duplicate definition of #[panic_implementation] - " << otherPath << " and " << path);
            }
        } else {
            ERROR(sp, E0000, "#[panic_implementation] on non-function " << path);
        }
    }
};

class DecoratorPanicHandler: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        if (i.is_Function()) {
            auto rv = crate.langItems.insert(::std::make_pair(::std::string("trustme-panic_implementation"), path));
            if (!rv.second) {
                const auto& otherPath = rv.first->second;
                ERROR(sp, E0000, "Duplicate definition of #[panic_handler] - " << otherPath << " and " << path);
            }
        } else {
            ERROR(sp, E0000, "#[panic_handler] on non-function " << path);
        }
    }
};

class DecoratorRustcStdInternalSymbol: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        // Attribute that acts as like `#[no_mangle]` `#[linkage="external"]`
    }
};

class DecoratorAllocErrorHandler: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        if (i.is_Function()) {
            auto rv = crate.langItems.insert(::std::make_pair(::std::string("trustme-alloc_error_handler"), path));
            if (!rv.second) {
                const auto& otherPath = rv.first->second;
                ERROR(sp, E0000, "Duplicate definition of #[alloc_error_handler] - " << otherPath << " and " << path);
            }
        }
    }
};

class DecoratorGlobalAllocator: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const ASTAttribute&, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute>, const ASTVisibility&, ASTItem& item) const override {
        if (!item.is_Static()) {
            ERROR(sp, E0000, "#[global_allocator] on non-static " << path);
        }
        auto rv = crate.langItems.insert(::std::make_pair(::std::string("trustme-global_allocator"), path));
        if (!rv.second && rv.first->second != path) {
            ERROR(sp, E0000, "Duplicate definition of #[global_allocator] - " << rv.first->second << " and " << path);
        }
    }
};

STATIC_DECORATOR("lang", DecoratorLangItem)
STATIC_DECORATOR("main", DecoratorMain);
STATIC_DECORATOR("start", DecoratorStart);
STATIC_DECORATOR("panic_implementation", DecoratorPanicImplementation);
STATIC_DECORATOR("panic_handler", DecoratorPanicHandler);
STATIC_DECORATOR("rustc_std_internal_symbol", DecoratorRustcStdInternalSymbol);
STATIC_DECORATOR("alloc_error_handler", DecoratorAllocErrorHandler);
STATIC_DECORATOR("global_allocator", DecoratorGlobalAllocator);

class CMultiHandlerLint: public ExpandDecorator {
    /// The level this attribute sets.
    virtual CfgLintLevel level() const = 0;

    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    /// Report the plain lint names in `(...)`. The list also carries entries
    /// this compiler has no lint for — tool lints (`clippy::foo`) and keyed
    /// entries (`reason = "..."`) — so the scan skips whatever it cannot read
    /// instead of rejecting the attribute.
    static void collectLintNames(const ASTAttribute& mi, const std::function<void(const RcString&)>& cb) {
        TTStream lex(mi.span(), ParseState(), mi.data());
        if (!lex.getTokenIf(TOK_PAREN_OPEN)) {
            return;
        }
        unsigned depth = 1;
        bool atName = true;
        while (depth > 0) {
            auto tok = lex.getToken();
            if (tok == TOK_EOF) {
                break;
            }
            if (tok == TOK_PAREN_OPEN) {
                depth += 1;
                atName = false;
                continue;
            }
            if (tok == TOK_PAREN_CLOSE) {
                depth -= 1;
                atName = true;
                continue;
            }
            if (tok == TOK_COMMA) {
                atName = (depth == 1);
                continue;
            }
            if (depth == 1 && atName && tok == TOK_IDENT) {
                const auto next = lex.lookahead(0);
                if (next == TOK_COMMA || next == TOK_PAREN_CLOSE) {
                    cb(tok.ident().name);
                }
            }
            atName = false;
        }
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override {
        collectLintNames(mi, [&](const RcString& name) {
            CfgSetLintLevel(*wb.settings, name.c_str(), this->level());
        });
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTStructItem& si) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTTupleItem& si) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTEnumVariant& ev) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeP& expr) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeMatchArm& expr) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeStructLiteral::Ent& expr) const override {
    }
};

class CHandlerAllow: public CMultiHandlerLint {
    CfgLintLevel level() const override {
        return CfgLintLevel::Allow;
    }
};

STATIC_DECORATOR("allow", CHandlerAllow);

class CHandlerWarn: public CMultiHandlerLint {
    CfgLintLevel level() const override {
        return CfgLintLevel::Warn;
    }
};

STATIC_DECORATOR("warn", CHandlerWarn);

class CHandlerDeny: public CMultiHandlerLint {
    CfgLintLevel level() const override {
        return CfgLintLevel::Deny;
    }
};

STATIC_DECORATOR("deny", CHandlerDeny);

class CHandlerForbid: public CMultiHandlerLint {
    CfgLintLevel level() const override {
        return CfgLintLevel::Forbid;
    }
};

STATIC_DECORATOR("forbid", CHandlerForbid);

// #[must_use] - Marks a type needing to be consumed
class CHandlerMustUse: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        // TODO: only allowed on types
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        // TODO: only allowed on associated types
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const override {
        // TODO: only allowed on associated types
    }
};

STATIC_DECORATOR("must_use", CHandlerMustUse);

// #[non_exhaustive] - Tag an enum as being extensible
class CHandlerNonExhaustive: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        // TODO: only allowed on types
    }
};

STATIC_DECORATOR("non_exhaustive", CHandlerNonExhaustive);

// #[path] - Already used by this stage
class CHandlerPath: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        // TODO: only allowed on modules
    }
};

STATIC_DECORATOR("path", CHandlerPath);

// #[rustc_promotable] - ?
class CHandlerRustcPromotable: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        // TODO: only allowed on functions?
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const override {
        // TODO: only allowed on functions?
    }
};

STATIC_DECORATOR("rustc_promotable", CHandlerRustcPromotable);

// #[rustc_inherit_overflow_checks]
class CHandlerRustcInheritOverflowChecks: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeP& expr) const override {
    }
};

STATIC_DECORATOR("rustc_inherit_overflow_checks", CHandlerRustcInheritOverflowChecks);

// #[rustc_on_unimplemented]
class CHandlerRustcOnUnimiplemented: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        // Trait only.
    }
};

STATIC_DECORATOR("rustc_on_unimplemented", CHandlerRustcOnUnimiplemented);

// #[rustc_box] - Marks the `Box::new` inner constructor
class CHandlerRustBox: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeP& expr) const override {
        auto* n = cast<ASTExprNodeCallPath>(expr.get());
        ASSERT_BUG(expr->span(), n, "");
        ASSERT_BUG(expr->span(), n->args.size() == 1, "");
        auto val = std::move(n->args[0]);
        auto span = n->span();
        expr.reset(new ASTExprNodeUniOp(ASTExprNodeUniOp::BOX, std::move(val)));
        expr->setSpan(span);
    }
};

STATIC_DECORATOR("rustc_box", CHandlerRustBox);

class CMultiHandlerStability: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTStructItem& si) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTTupleItem& si) const override {
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTEnumVariant& ev) const override {
    }
};

class CHandlerStable: public CMultiHandlerStability {};

STATIC_DECORATOR("stable", CHandlerStable);

class CHandlerUnstable: public CMultiHandlerStability {};

STATIC_DECORATOR("unstable", CHandlerUnstable);

class CHandlerRustcDeprecated: public CMultiHandlerStability {};

STATIC_DECORATOR("rustc_deprecated", CHandlerRustcDeprecated);

// #[rustc_const_unstable] - Unstable in const context
class CHandlerRustcConstUnstable: public CMultiHandlerStability {};

STATIC_DECORATOR("rustc_const_unstable", CHandlerRustcConstUnstable);

class CHandlerDeprecated: public CMultiHandlerStability {};

STATIC_DECORATOR("deprecated", CHandlerDeprecated);

class CHandlerAllowInternalUnstable: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
    }
};

STATIC_DECORATOR("allow_internal_unstable", CHandlerAllowInternalUnstable);

class DecoratorNoStd: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override {
        if (crate.loadStd != ASTCrate::LOAD_STD && crate.loadStd != ASTCrate::LOAD_CORE) {
            WARNING(sp, W0000, "Use of #![no_std] with itself or #![no_core]");
            return;
        }
        crate.loadStd = ASTCrate::LOAD_CORE;
    }
};

class DecoratorNoCore: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override {
        if (crate.loadStd != ASTCrate::LOAD_STD && crate.loadStd != ASTCrate::LOAD_NONE) {
            WARNING(sp, W0000, "Use of #![no_core] with itself or #![no_std]");
        }
        crate.loadStd = ASTCrate::LOAD_NONE;
    }
};

class DecoratorNoMain: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span&, const ASTAttribute&, const WireBoard&, ASTCrate& crate) const override {
        crate.noMain = true;
    }
};

//class Decorator_Prelude:
//    public ExpandDecorator
//{
//public:
//    AttrStage stage() const override { return AttrStage::Pre; }

class DecoratorNoPrelude: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        if (i.is_Module()) {
            i.as_Module().insertPrelude = false;
        } else {
            ERROR(sp, E0000, "Invalid use of #[no_prelude] on non-module");
        }
    }
};

class DecoratorPreludeImport: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        if (const auto* e = i.opt_Use()) {
            if (e->entries.size() != 1) {
                ERROR(sp, E0000, "#[prelude_import] should be on a single-entry use");
            }
            ASSERT_BUG(sp, path.nodes.size() > 0, path);
            ASSERT_BUG(sp, path.nodes.back() == "", path);
            if (e->entries.front().name != "") {
                ERROR(sp, E0000, "#[prelude_import] should be on a glob");
            }
            const auto& p = e->entries.front().path;
            // TODO: Ensure that this statement is a glob (has a name of "")
            if (p.isRelative()) {
                crate.preludePath = ASTPath(path);
                crate.preludePath.nodes().pop_back();
                crate.preludePath += p;
            } else {
                crate.preludePath = ASTPath(p);
            }
        } else {
            ERROR(sp, E0000, "Invalid use of #[prelude_import] on non-use");
        }
    }
};

void ExpandInitStdPrelude() {
    RegisterSynextDecoratorG<DecoratorNoStd>("no_std");
    RegisterSynextDecoratorG<DecoratorNoCore>("no_core");
    RegisterSynextDecoratorG<DecoratorNoMain>("no_main");
    RegisterSynextDecoratorG<DecoratorPreludeImport>("prelude_import");
    RegisterSynextDecoratorG<DecoratorNoPrelude>("no_prelude");
}

class CTestHandler: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    } // Expand early so tests are removed before inner expansion

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        if (!i.is_Function()) {
            ERROR(sp, E0000, "#[test] can only be put on functions - found on " << i.tagStr());
        }

        if (crate.testHarness) {
            ASTTestDesc td;
            td.span = sp;
            for (const auto& node : path.nodes) {
                td.name += "::";
                td.name += node.c_str();
            }
            td.path = path;

            crate.tests.push_back(mv$(td));
        } else {
            i = ASTItem::make_None({});
        }
    }
};

class CTestHandlerSP: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        if (!i.is_Function()) {
            ERROR(sp, E0000, "#[should_panic] can only be put on functions - found on " << i.tagStr());
        }

        if (crate.testHarness) {
            // TODO: If this test doesn't yet exist, create it (but as disabled)?
            for (auto& td : crate.tests) {
                if (td.path != path) {
                    continue;
                }

                if (mi.data().size() != 0) {
                    td.panicType = ASTTestDesc::ShouldPanic::YesWithMessage;

                    TTStream lex(sp, ParseState(), mi.data());
                    lex.parseState().wb = &wb;
                    auto parseMessage = [&]() {
                        auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
                        if (auto* v = cast<ASTExprNodeString>(&*n)) {
                            td.expectedPanicMessage = v->value;
                        } else {
                            throw ParseErrorUnexpected(lex, Token(InterpolatedFragment(InterpolatedFragment::EXPR, n.release())), TOK_STRING);
                        }
                    };
                    if (lex.getTokenIf(TOK_EQUAL)) {
                        parseMessage();
                    } else {
                        // Anything other than exactly `expected = "..."` is a
                        // lint in rustc, and the attribute still means "this
                        // test panics" -- just without a message to match.
                        bool gotMessage = false;
                        if (lex.getTokenIf(TOK_PAREN_OPEN) && lex.lookahead(0) == TOK_IDENT && lex.lookahead(1) == TOK_EQUAL) {
                            auto n = lex.getTokenCheck(TOK_IDENT).ident().name;
                            lex.getTokenCheck(TOK_EQUAL);
                            if (n == "expected" && lex.lookahead(0) == TOK_STRING) {
                                parseMessage();
                                gotMessage = lex.lookahead(0) == TOK_PAREN_CLOSE;
                            }
                        }
                        if (!gotMessage) {
                            td.panicType = ASTTestDesc::ShouldPanic::Yes;
                            td.expectedPanicMessage = "";
                        }
                    }
                } else {
                    td.panicType = ASTTestDesc::ShouldPanic::Yes;
                }
                return;
            }
            //ERROR()
        }
    }
};

class CTestHandlerIgnore: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        if (!i.is_Function()) {
            ERROR(sp, E0000, "#[ignore] can only be put on functions - found on " << i.tagStr());
        }

        if (crate.testHarness) {
            for (auto& td : crate.tests) {
                if (td.path != path) {
                    continue;
                }

                td.ignore = true;
                return;
            }
            //ERROR()
        }
    }
};

STATIC_DECORATOR("test", CTestHandler);
STATIC_DECORATOR("should_panic", CTestHandlerSP);
STATIC_DECORATOR("ignore", CTestHandlerIgnore);

DecoratorDef::DecoratorDef(::std::string name, ::std::unique_ptr<ExpandDecorator> def)
    : prev(nullptr)
    , name(::std::move(name))
    , def(::std::move(def))
{
    RegisterSynextDecoratorStatic(this);
}

bool ExpandDecorator::runDuringIter() const {
    return false;
}

// Whether `handle` should receive the item's full attribute list instead of only the
// attributes written after the invoking one (derive macros need the full set).
bool ExpandDecorator::wantsAllAttrs() const {
    return false;
}

void ExpandDecorator::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const {
    unexpected(sp, mi, "crate");
}

void ExpandDecorator::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t modIdx, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const {
    unexpected(sp, mi, "item");
}

void ExpandDecorator::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const {
    unexpected(sp, mi, "associated item");
}

void ExpandDecorator::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const {
    unexpected(sp, mi, "trait item");
}

// NOTE: To delete, clear the name
void ExpandDecorator::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTStructItem& si) const {
    unexpected(sp, mi, "struct item");
}

// NOTE: To delete, make the type invalid
void ExpandDecorator::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTTupleItem& si) const {
    unexpected(sp, mi, "tuple item");
}

// NOTE: To delete, clear the name
void ExpandDecorator::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTEnumVariant& ev) const {
    unexpected(sp, mi, "enum variant");
}

void ExpandDecorator::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeP& expr) const {
    unexpected(sp, mi, "expression");
}

// NOTE: To delete, clear the patterns vector
void ExpandDecorator::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeMatchArm& expr) const {
    unexpected(sp, mi, "match arm");
}

// NOTE: To delete, clear the value
void ExpandDecorator::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeStructLiteral::Ent& expr) const {
    unexpected(sp, mi, "struct literal ent");
}
