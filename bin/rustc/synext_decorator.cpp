#include "synext_decorator.h"

#include "synext.h"
#include "ast_generics.h"
#include "ast_ast.h"
#include "parse_ttstream.h"
#include "expand_cfg.h"
#include "ast_crate.h"
#include "common.h"
#include "ast_expr.h"
#include "hir_hir.h"      // ABI_RUST
#include "parse_common.h" // Parse_ModRoot_Items
#include "expand_proc_macro.h"
#include "expand_common.h" // Expand_LookupMacro
#include "parse_parseerror.h" // ParseError
#include "parse_interpolated_fragment.h"

namespace {
    class CommonFunction: public ExpandDecorator {
    public:
        virtual void handle(const AST::Attribute& mi, AST::Function& fcn) const = 0;

        AttrStage stage() const override {
            return AttrStage::Pre;
        }

        void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module&, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
            if (i.is_None()) {
            } else if (i.is_Function()) {
                this->handle(mi, i.as_Function());
            } else {
                // TODO: Error
            }
        }

        void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, AST::Impl& impl, const RcString& name, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
            if (i.is_None()) {
            } else if (i.is_Function()) {
                this->handle(mi, i.as_Function());
            } else {
                // TODO: Error
            }
        }

        void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, const AST::AbsolutePath& path, AST::Trait& trait, slice<const AST::Attribute> attrs, AST::Item& i) const override {
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
    void handle(const AST::Attribute& mi, AST::Function& fcn) const override {
        TTStream lex(mi.span(), ParseState(), mi.data());
        //ASSERT_BUG(mi.span(), fcn.m_markings.inline_type == AST::Function::Markings::Inline::Auto, "Duplicate #[inline] attributes");
        if (lex.getTokenIf(TOK_PAREN_OPEN)) {
            auto attr = lex.getTokenCheck(TOK_IDENT).ident().name;
            if (attr == "never") {
                fcn.markings.inlineType = AST::Function::Markings::Inline::Never;
            } else if (attr == "always") {
                fcn.markings.inlineType = AST::Function::Markings::Inline::Always;
            } else {
                ERROR(lex.pointSpan(), E0000, "Unknown inline type #[inline(" << attr << ")]");
            }
            lex.getTokenCheck(TOK_PAREN_CLOSE);
            lex.getTokenCheck(TOK_EOF);
        } else {
            fcn.markings.inlineType = AST::Function::Markings::Inline::Normal;
        }
    }
};

STATIC_DECORATOR("inline", CHandlerInline);

class CHandlerCold: public CommonFunction {
public:
    void handle(const AST::Attribute& mi, AST::Function& fcn) const override {
        TTStream lex(mi.span(), ParseState(), mi.data());
        lex.getTokenCheck(TOK_EOF);
        //ASSERT_BUG(mi.span(), !fcn.m_markings.is_cold, "Duplicate #[cold] attributes");
        fcn.markings.isCold = true;
    }
};

STATIC_DECORATOR("cold", CHandlerCold);

class CHandlerRustcLegacyConstGenerics: public CommonFunction {
    void handle(const AST::Attribute& mi, AST::Function& fcn) const override {
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

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        if (i.is_None()) {
        }
        // --- struct ---
        else if (auto* s = i.opt_Struct()) {
            TTStream lex(sp, ParseState(), mi.data());
            lex.getTokenCheck(TOK_PAREN_OPEN);
            do {
                auto reprType = lex.getTokenCheck(TOK_IDENT).ident().name;
                if (reprType == "C") {
                    switch (s->markings.repr) {
                        case AST::Struct::Markings::Repr::Rust:
                            s->markings.repr = AST::Struct::Markings::Repr::C;
                            break;
                        default:
                            // TODO: Error
                            break;
                    }
                } else if (reprType == "packed") {
                    switch (s->markings.repr) {
                        case AST::Struct::Markings::Repr::C:
                        case AST::Struct::Markings::Repr::Rust:
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
                        auto* val = cast<AST::ExprNodeInteger>(&*n);
                        ASSERT_BUG(n->span(), val, "#[repr(packed(...))] - alignment must be an integer");
                        auto v = val->mValue;
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
                    s->markings.repr = AST::Struct::Markings::Repr::Simd;
                } else if (reprType == "transparent") {
                    s->markings.repr = AST::Struct::Markings::Repr::Transparent;
                } else if (reprType == "align") {
                    lex.getTokenCheck(TOK_PAREN_OPEN);
                    auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
                    auto* val = cast<AST::ExprNodeInteger>(&*n);
                    ASSERT_BUG(n->span(), val, "#[repr(align(...))] - alignment must be an integer");
                    auto v = val->mValue;
                    ASSERT_BUG(lex.pointSpan(), v > U128(0), "#[repr(align(" << v << "))] - alignment must be non-zero");
                    ASSERT_BUG(lex.pointSpan(), (v & (v - 1)) == U128(0), "#[repr(align(" << v << "))] - alignment must be a power of two");
                    s->markings.alignValue = std::max(s->markings.alignValue, v.truncateU64());
                    lex.getTokenCheck(TOK_PAREN_CLOSE);
                } else if (reprType == "no_niche") {
                    // TODO: rust-lang/rust#68303 happens with UnsafeCell and niche optionisations
                    // - Would mrustc also have this?
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
            lex.getTokenCheck(TOK_PAREN_OPEN);

            // Loop, so `repr(C, u8)` is valid
            while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
                auto setRepr = [&](::AST::Enum::Markings::Repr r) {
                    ASSERT_BUG(lex.pointSpan(), e->markings.repr == ::AST::Enum::Markings::Repr::Rust, "Multiple enum reprs set");
                    e->markings.repr = r;
                };
                auto reprStr = lex.getTokenCheck(TOK_IDENT).ident().name;
                if (reprStr == "C") {
                    // Repeated is OK
                    e->markings.isReprC = true;
                } else if (reprStr == "u8") {
                    setRepr(::AST::Enum::Markings::Repr::U8);
                } else if (reprStr == "u16") {
                    setRepr(::AST::Enum::Markings::Repr::U16);
                } else if (reprStr == "u32") {
                    setRepr(::AST::Enum::Markings::Repr::U32);
                } else if (reprStr == "u64") {
                    setRepr(::AST::Enum::Markings::Repr::U64);
                } else if (reprStr == "u128") {
                    setRepr(::AST::Enum::Markings::Repr::U128);
                } else if (reprStr == "usize") {
                    setRepr(::AST::Enum::Markings::Repr::Usize);
                } else if (reprStr == "i8") {
                    setRepr(::AST::Enum::Markings::Repr::I8);
                } else if (reprStr == "i16") {
                    setRepr(::AST::Enum::Markings::Repr::I16);
                } else if (reprStr == "i32") {
                    setRepr(::AST::Enum::Markings::Repr::I32);
                } else if (reprStr == "i64") {
                    setRepr(::AST::Enum::Markings::Repr::I64);
                } else if (reprStr == "i128") {
                    setRepr(::AST::Enum::Markings::Repr::I128);
                } else if (reprStr == "isize") {
                    setRepr(::AST::Enum::Markings::Repr::Isize);
                } else if (reprStr == "align") {
                    lex.getTokenCheck(TOK_PAREN_OPEN);
                    auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
                    auto* val = cast<AST::ExprNodeInteger>(&*n);
                    ASSERT_BUG(n->span(), val, "#[repr(align(...))] - alignment must be an integer");
                    auto v = val->mValue;
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
            lex.getTokenCheck(TOK_PAREN_OPEN);

            do {
                auto reprStr = lex.getTokenCheck(TOK_IDENT).ident().name;
                if (reprStr == "C") {
                    e->markings.repr = ::AST::Union::Markings::Repr::C;
                } else if (reprStr == "transparent") {
                    e->markings.repr = ::AST::Union::Markings::Repr::Transparent;
                } else if (reprStr == "packed") {
                    //switch( e->m_markings.repr )
                    //{
                    //case AST::Struct::Markings::Repr::C:
                    //case AST::Struct::Markings::Repr::Rust:
                    //    break;
                    //default:
                    //    // TODO: Error
                    //    break;
                    //}
                    //if( e->m_markings.max_field_align != 0 ) {
                    //    // TODO: Error
                    //}
                    if (lex.getTokenIf(TOK_PAREN_OPEN)) {
                        auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
                        auto* val = cast<AST::ExprNodeInteger>(&*n);
                        ASSERT_BUG(n->span(), val, "#[repr(packed(...))] - alignment must be an integer");
                        auto v = val->mValue;
                        ASSERT_BUG(lex.pointSpan(), v > U128(0), "#[repr(packed(" << v << "))] - alignment must be non-zero");
                        ASSERT_BUG(lex.pointSpan(), (v & (v - 1)) == U128(0), "#[repr(packed(" << v << "))] - alignment must be a power of two");
                        //ASSERT_BUG(lex.point_span(), e->m_markings.align_value == 0, "#[repr(packed(" << v << "))] - conflicts with previous alignment");
                        // TODO: I believe this should change the internal aligment too?
                        //e->m_markings.max_field_align = v.truncate_u64();
                        lex.getTokenCheck(TOK_PAREN_CLOSE);
                    } else {
                        //e->m_markings.max_field_align = 1;
                    }
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

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
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

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        // TODO: Types only
        if (auto* s = i.opt_Struct()) {
            TTStream lex(sp, ParseState(), mi.data());
            lex.getTokenCheck(TOK_PAREN_OPEN);
            auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
            auto* np = cast<AST::ExprNodeInteger>(n.get());
            ASSERT_BUG(n->span(), np, "#[rustc_layout_scalar_valid_range_start] requires an integer - got " << FMT_CB(ss, n->print(ss)));
            lex.getTokenCheck(TOK_PAREN_CLOSE);
            lex.getTokenCheck(TOK_EOF);

            s->markings.scalarValidStartSet = true;
            s->markings.scalarValidStart = np->mValue;
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

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        // TODO: Types only
        if (auto* s = i.opt_Struct()) {
            TTStream lex(sp, ParseState(), mi.data());
            lex.getTokenCheck(TOK_PAREN_OPEN);
            auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
            auto* np = cast<AST::ExprNodeInteger>(n.get());
            ASSERT_BUG(n->span(), np, "#[rustc_layout_scalar_valid_range_end] requires an integer - got " << FMT_CB(ss, n->print(ss)));
            lex.getTokenCheck(TOK_PAREN_CLOSE);
            lex.getTokenCheck(TOK_EOF);
            s->markings.scalarValidEndSet = true;
            s->markings.scalarValidEnd = np->mValue;
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

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        auto linkName = mi.parseEqualsString(crate, mod);
        ASSERT_BUG(sp, linkName != "", "Empty #[link_name] attribute");

        if (i.is_None()) {
        } else if (auto* fcn = i.opt_Function()) {
            ASSERT_BUG(sp, fcn->markings.linkName == "", "Duplicate #[link_name] attributes");
            fcn->markings.linkName = linkName;
        } else if (auto* st = i.opt_Static()) {
            ASSERT_BUG(sp, st->sClass() != ::AST::Static::CONST, "#[link_name] on `const`");
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

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        auto linkSection = mi.parseEqualsString(crate, mod);
        ASSERT_BUG(sp, linkSection != "", "Empty #[link_section] attribute");

        if (i.is_None()) {
        } else if (auto* fcn = i.opt_Function()) {
            ASSERT_BUG(sp, fcn->markings.linkSection == "", "Duplicate #[link_section] attributes");
            fcn->markings.linkSection = linkSection;
        } else if (auto* st = i.opt_Static()) {
            ASSERT_BUG(sp, st->sClass() != ::AST::Static::CONST, "#[link_section] on `const`");
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

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module&, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        if (i.is_None()) {
        } else if (auto* b = i.opt_ExternBlock()) {
            TTStream lex(sp, ParseState(), mi.data());
            lex.getTokenCheck(TOK_PAREN_OPEN);
            std::string libName;
            bool emit = true;
            AST::ExternBlock::Link link;

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
                    emit &= checkCfgStream(lex);
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

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module&, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        TTStream lex(sp, ParseState(), mi.data());
        lex.getTokenCheck(TOK_EQUAL);
        auto tok = lex.getTokenCheck(TOK_STRING);
        auto linkageStr = tok.str();
        lex.getTokenCheck(TOK_EOF);

        auto linkage = AST::Linkage::Default;
        if (linkageStr == "extern_weak") {
            linkage = AST::Linkage::ExternWeak;
        } else if (linkageStr == "weak") {
            linkage = AST::Linkage::Weak;
        } else if (linkageStr == "external") {
            //linkage = AST::Linkage::External;
        } else {
            TODO(sp, "#[linkage=\"" << linkageStr << "\"]");
        }

        if (auto* f = i.opt_Function()) {
            switch (linkage) {
                case AST::Linkage::Default:
                case AST::Linkage::Weak:
                    break;
                default:
                    TODO(sp, "#[linkage=\"" << linkageStr << "\"] on " << i.tagStr());
            }
            f->markings.linkage = linkage;
        } else if (auto* f = i.opt_Static()) {
            switch (linkage) {
                case AST::Linkage::Default:
                case AST::Linkage::Weak:
                case AST::Linkage::ExternWeak:
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

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module&, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        // TODO: Only valid on functions?
    }
};

STATIC_DECORATOR("target_feature", CHandlerTargetFeature);

class CHandlerRustcIntrinsic: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module&, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
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
        return AttrStage::Post;
    }

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module&, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        if (/*auto* e =*/i.opt_Function()) {
            // Handled by HIR lower
        } else {
            ERROR(sp, E0000, "#[track_caller] on non-function");
        }
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, AST::Impl& impl, const RcString& name, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        if (/*auto* e =*/i.opt_Function()) {
            // Handled by HIR lower
        } else {
            ERROR(sp, E0000, "#[track_caller] on non-function");
        }
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, const AST::AbsolutePath& path, AST::Trait& trait, slice<const AST::Attribute> attrs, AST::Item& i) const override {
        if (/*auto* e =*/i.opt_Function()) {
            // Handled by HIR lower
        } else {
            ERROR(sp, E0000, "#[track_caller] on non-function");
        }
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::ExprNodeP& expr) const override {
        if (auto* n = cast<AST::ExprNodeClosure>(expr.get())) {
            //n->m_track_caller = true;
            (void)n;
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

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module&, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        TTStream lex(mi.span(), ParseState(), mi.data());
        lex.getTokenCheck(TOK_PAREN_OPEN);
        while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
            auto ident = lex.getTokenCheck(TOK_IDENT).ident().name;

            if (ident == "no_mangle") {
                DEBUG("#[unsafe(no_mangle)] " << path << " = " << path.nodes.back().c_str());
                if (auto* e = i.opt_Function()) {
                    e->markings.linkName = path.nodes.back().c_str();
                } else if (auto* e = i.opt_Static()) {
                    e->markings.linkName = path.nodes.back().c_str();
                } else {
                    ERROR(sp, E0000, "#[unsafe(" << ident << ")] on bad item: " << i.tagStr());
                }
            } else if (ident == "link_section") {
                lex.getTokenCheck(TOK_EQUAL);
                auto s = lex.getTokenCheck(TOK_STRING).str();

                DEBUG("#[unsafe(link_section)] " << path << " in `" << s);
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
};

STATIC_DECORATOR("unsafe", CHandlerUnsafe);


class DecoratorCrateType: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate) const override {
        auto name = mi.parseEqualsString(crate, crate.mRootModule);
        if (name == "rlib" || name == "lib") {
            crate.crateType = AST::Crate::Type::RustLib;
        } else if (name == "dylib" || name == "rdylib") {
            crate.crateType = AST::Crate::Type::RustDylib;
        } else if (name == "cdylib") {
            crate.crateType = AST::Crate::Type::CDylib;
        } else if (name == "proc-macro") {
            crate.crateType = AST::Crate::Type::ProcMacro;
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

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate) const override {
        auto name = mi.parseEqualsString(crate, crate.mRootModule);
        crate.setCrateName(name);
    }
};

class DecoratorFeature: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate) const override {
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

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate) const override {
        // TODO: Check for an existing allocator crate
        crate.mLangItems.insert(::std::make_pair("mrustc-allocator", AST::AbsolutePath()));
    }

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
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

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate) const override {
        // TODO: Check for an existing panic_runtime crate
        crate.mLangItems.insert(::std::make_pair("mrustc-panic_runtime", AST::AbsolutePath()));
    }
};

class DecoratorNeedsPanicRuntime: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate) const override {
        crate.mLangItems.insert(::std::make_pair("mrustc-needs_panic_runtime", AST::AbsolutePath()));
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

    TypeRef mktypeSelf(const Span& sp) {
        return TypeRef(sp, rcstringSelf, 0xFFFF);
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

static AST::Path getPath(const RcString& coreName, const char* c1, const char* c2) {
    return AST::AbsolutePath(coreName, {RcString::newInterned(c1), RcString::newInterned(c2)});
}

static AST::Path getPath(const RcString& coreName, const char* c1, const char* c2, const char* c3) {
    return AST::AbsolutePath(coreName, {RcString::newInterned(c1), RcString::newInterned(c2), RcString::newInterned(c3)});
}

static std::unique_ptr<AST::ExprNodeBlock> newBlock(const Span& sp) {
    auto rv = ::std::make_unique<AST::ExprNodeBlock>();
    rv->setSpan(sp);
    return rv;
}

static inline AST::ExprNodeP mkExprnodep(AST::ExprNode* en) {
    return AST::ExprNodeP(en);
}

//#define NEWNODE(type, ...)  mk_exprnodep(new type(__VA_ARGS__))
#define NEWNODE(type, ...) mkExprnodep(new AST::ExprNode##type(__VA_ARGS__))

static void makeRefpatA(const Span& sp, AST::ExprNodeBlock& block, ::std::vector<AST::Pattern>& patsA, const ::std::vector<AST::TupleItem>& subTypes, ::std::function<AST::ExprNodeP(size_t, AST::ExprNodeP)> cb) {
    ::std::vector<AST::ExprNodeBlock::Line> nodes;
    for (size_t idx = 0; idx < subTypes.size(); idx++) {
        auto nameA = RcString::newInterned(FMT("a" << idx));
        patsA.push_back(::AST::Pattern(::AST::Pattern::TagBind(), sp, nameA, ::AST::PatternBinding::Type::REF));
        block.pushStmt(cb(idx, NEWNODE(NamedValue, AST::Path(nameA))));
    }
}

static void makeRefpatA(const Span& sp, AST::ExprNodeBlock& block, ::std::vector<AST::StructPatternEntry>& patsA, const ::std::vector<AST::StructItem>& fields, ::std::function<AST::ExprNodeP(size_t, AST::ExprNodeP)> cb) {
    ::std::vector<AST::ExprNodeBlock::Line> nodes;
    size_t idx = 0;
    for (const auto& fld : fields) {
        auto nameA = RcString::newInterned(FMT("a" << fld.mName));
        patsA.push_back(AST::StructPatternEntry{AST::AttributeList(), fld.mName, ::AST::Pattern(::AST::Pattern::TagBind(), sp, nameA, ::AST::PatternBinding::Type::REF)});
        block.pushStmt(cb(idx, NEWNODE(NamedValue, AST::Path(nameA))));
        idx++;
    }
}

static void makeRefpatAb(const Span& sp, AST::ExprNodeBlock& block, ::std::vector<AST::Pattern>& patsA, ::std::vector<AST::Pattern>& patsB, const ::std::vector<AST::TupleItem>& subTypes, ::std::function<AST::ExprNodeP(size_t, AST::ExprNodeP, AST::ExprNodeP)> cb) {
    for (size_t idx = 0; idx < subTypes.size(); idx++) {
        auto nameA = RcString::newInterned(FMT("a" << idx));
        auto nameB = RcString::newInterned(FMT("b" << idx));
        patsA.push_back(::AST::Pattern(::AST::Pattern::TagBind(), sp, nameA, ::AST::PatternBinding::Type::REF));
        patsB.push_back(::AST::Pattern(::AST::Pattern::TagBind(), sp, nameB, ::AST::PatternBinding::Type::REF));
        block.pushStmt(cb(idx, NEWNODE(NamedValue, AST::Path(nameA)), NEWNODE(NamedValue, AST::Path(nameB))));
    }
}

static void makeRefpatAb(const Span& sp, AST::ExprNodeBlock& block, ::std::vector<AST::StructPatternEntry>& patsA, ::std::vector<AST::StructPatternEntry>& patsB, const ::std::vector<AST::StructItem>& fields, ::std::function<AST::ExprNodeP(size_t, AST::ExprNodeP, AST::ExprNodeP)> cb) {
    size_t idx = 0;
    for (const auto& fld : fields) {
        auto nameA = RcString::newInterned(FMT("a" << fld.mName));
        auto nameB = RcString::newInterned(FMT("b" << fld.mName));
        patsA.push_back(AST::StructPatternEntry{AST::AttributeList(), fld.mName, ::AST::Pattern(::AST::Pattern::TagBind(), sp, nameA, ::AST::PatternBinding::Type::REF)});
        patsB.push_back(AST::StructPatternEntry{AST::AttributeList(), fld.mName, ::AST::Pattern(::AST::Pattern::TagBind(), sp, nameB, ::AST::PatternBinding::Type::REF)});
        block.pushStmt(cb(idx, NEWNODE(NamedValue, AST::Path(nameA)), NEWNODE(NamedValue, AST::Path(nameB))));
        idx++;
    }
}

struct DeriveOpts {
    RcString coreName;
};

/// Interface for derive handlers
struct Deriver {
    virtual ~Deriver() = default;
    virtual const char* traitName() const = 0;
    virtual AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Struct& str) const = 0;
    virtual AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Enum& enm) const = 0;

    virtual AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Union& unn) const {
        ERROR(sp, E0000, "Cannot derive(" << traitName() << ") on union");
    }

    void iterateStructFields(const AST::Struct& str, ::std::function<void(RcString)> cb) const {
        TU_MATCH_HDRA((str.mData), {)
        TU_ARMA(Unit, e) {
            }
            TU_ARMA(Struct, e) {
                for (const auto& fld : e.ents) {
                    cb(fld.mName);
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

    AST::GenericParams getParamsWithBounds(const Span& sp, const AST::GenericParams& p, const AST::Path& traitPath, ::std::vector<TypeRef> additionalBoundedTypes) const {
        AST::GenericParams params = p.clone();

        // TODO: Get bounds based on generic (or similar) types used within the type.
        // - How would this code (that runs before resolve) know what's a generic and what's a local type?
        // - Searches within the type for a Path that starts with that param.

        unsigned int i = 0;
        for (const auto& arg : params.mParams) {
            if (const auto* e = arg.opt_Type()) {
                params.addBound(::AST::GenericBound::make_IsTrait({sp, {}, TypeRef(sp, e->name(), i), {}, traitPath}));
                i++;
            }
        }

        // For each field type
        // - Locate used generic parameters in the type (and sub-types that directly use said parameter)
        for (auto& ty : additionalBoundedTypes) {
            params.addBound(::AST::GenericBound::make_IsTrait({sp, {}, mv$(ty), {}, traitPath}));
        }

        return params;
    }

    ::std::vector<TypeRef> getFieldBounds(const AST::Struct& str) const {
        ::std::vector<TypeRef> ret;
        TU_MATCH(AST::StructData, (str.mData), (e), (Unit, ), (Struct, for (const auto& fld : e.ents) { addFieldBoundFromTy(str.params(), ret, fld.mType); }), (Tuple, for (const auto& ent : e.ents) { addFieldBoundFromTy(str.params(), ret, ent.mType); }))
        return ret;
    }

    ::std::vector<TypeRef> getFieldBounds(const AST::Enum& enm) const {
        ::std::vector<TypeRef> ret;

        for (const auto& v : enm.variants()) {
            TU_MATCH(::AST::EnumVariantData, (v.mData), (e), (Unit, ), (Tuple, for (const auto& ent : e.mItems) { addFieldBoundFromTy(enm.params(), ret, ent.mType); }), (Struct, for (const auto& fld : e.fields) { addFieldBoundFromTy(enm.params(), ret, fld.mType); }))
        }

        return ret;
    }

    ::std::vector<TypeRef> getFieldBounds(const AST::Union& unn) const {
        ::std::vector<TypeRef> ret;
        for (const auto& fld : unn.mVariants) {
            addFieldBoundFromTy(unn.params(), ret, fld.mType);
        }
        return ret;
    }

    void addFieldBoundFromTy(const AST::GenericParams& params, ::std::vector<TypeRef>& outList, const TypeRef& ty) const {
        struct H {
            static void visitNodes(const Deriver& self, const AST::GenericParams& params, ::std::vector<TypeRef>& outList, const ::std::vector<AST::PathNode>& nodes) {
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
        TU_MATCH_HDRA( (ty.mData), {)
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
                addFieldBoundFromTy(params, outList, *e.inner);
            }
            TU_ARMA(Pointer, e) {
                addFieldBoundFromTy(params, outList, *e.inner);
            }
            TU_ARMA(Array, e) {
                addFieldBoundFromTy(params, outList, *e.inner);
            }
            TU_ARMA(Slice, e) {
                addFieldBoundFromTy(params, outList, *e.inner);
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
                            for (const auto& param : params.mParams) {
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

    void addFieldBound(::std::vector<TypeRef>& outList, const TypeRef& type) const {
        for (const auto& ty : outList) {
            if (ty == type) {
                return;
            }
        }

        outList.push_back(type.clone());
    }
};

/// 'Debug' derive handler
class DeriverDebug: public Deriver {
    AST::Impl makeRet(Span sp, const RcString& coreName, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> typesToBound, AST::ExprNodeP node) const {
        const AST::Path debugTrait = getPath(coreName, "fmt", "Debug");

        AST::Function fcn(sp, TypeRef(sp, getPath(coreName, "fmt", "Result")), vec$(AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstringSelfLower), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), false, mktypeSelf(sp))), AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstringF), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), true, TypeRef(sp, getPath(coreName, "fmt", "Formatter"))))));
        fcn.setCode(NEWNODE(Block, mv$(node)));

        AST::GenericParams params = getParamsWithBounds(sp, p, debugTrait, mv$(typesToBound));

        AST::Impl rv(AST::ImplDef(mv$(params), makeSpanned(sp, debugTrait), type.clone()));
        rv.addFunction(sp, {}, AST::Visibility::makeBarePrivate(), false, rcstringFmt, mv$(fcn));
        return mv$(rv);
    }

public:
    const char* traitName() const override {
        return "Debug";
    }

    AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Struct& str) const override {
        ::std::string name = type.path().nodes().back().name().c_str();

        // Generate code for Debug
        AST::ExprNodeP node;
        TU_MATCH_HDRA((str.mData), {)
        TU_ARMA(Unit, e) {
                node = NEWNODE(NamedValue, AST::Path(rcstringF));
                node = NEWNODE(CallMethod, mv$(node), AST::PathNode(rcstringWriteStr, {}), vec$(NEWNODE(String, name)));
            }
            TU_ARMA(Struct, e) {
                node = NEWNODE(NamedValue, AST::Path(rcstringF));
                std::vector<AST::ExprNodeBlock::Line> nodes;
                nodes.push_back({true, NEWNODE(LetBinding, AST::Pattern(AST::Pattern::TagBind(), sp, rcstringS), TypeRef(sp), NEWNODE(CallMethod, mv$(node), AST::PathNode(RcString::newInterned("debug_struct"), {}), vec$(NEWNODE(String, name))))});
                for (const auto& fld : e.ents) {
                    nodes.push_back({true, NEWNODE(CallMethod, NEWNODE(NamedValue, AST::Path(rcstringS)), AST::PathNode(rcstringField, {}), vec$(NEWNODE(String, fld.mName.c_str()), NEWNODE(UniOp, AST::ExprNodeUniOp::REF, NEWNODE(UniOp, AST::ExprNodeUniOp::REF, NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstringSelfLower)), fld.mName)))))});
                }
                nodes.push_back({false, NEWNODE(CallMethod, NEWNODE(NamedValue, AST::Path(rcstringS)), AST::PathNode(rcstringFinish, {}), {})});
                node = NEWNODE(Block, mv$(nodes));
            }
            TU_ARMA(Tuple, e) {
                node = NEWNODE(NamedValue, AST::Path(rcstringF));
                node = NEWNODE(CallMethod, mv$(node), AST::PathNode(RcString::newInterned("debug_tuple"), {}), vec$(NEWNODE(String, name)));
                for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                    node = NEWNODE(CallMethod, mv$(node), AST::PathNode(rcstringField, {}), vec$(NEWNODE(UniOp, AST::ExprNodeUniOp::REF, NEWNODE(UniOp, AST::ExprNodeUniOp::REF, NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstringSelfLower)), RcString::newInterned(FMT(idx)))))));
                }
                node = NEWNODE(CallMethod, mv$(node), AST::PathNode(rcstringFinish, {}), {});
            }
        }

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), mv$(node));
    }

    AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Enum& enm) const override {
        AST::Path basePath = *type.mData.as_Path();
        basePath.nodes().back() = basePath.nodes().back().name();

        ::std::vector<AST::ExprNodeMatchArm> arms;
        for (const auto& v : enm.variants()) {
            AST::ExprNodeP code;
            AST::Pattern patA;

            AST::Path variantPath = basePath + v.mName;

            TU_MATCH_HDRA( (v.mData), {)
            TU_ARMA(Unit, e) {
                    code = NEWNODE(CallMethod, NEWNODE(NamedValue, AST::Path(rcstringF)), AST::PathNode(rcstringWriteStr, {}), vec$(NEWNODE(String, v.mName.c_str())));
                    patA = AST::Pattern(AST::Pattern::TagValue(), sp, AST::Pattern::Value::make_Named(variantPath));
                }
                TU_ARMA(Tuple, e) {
                    ::std::vector<AST::Pattern> patsA;
                    auto block = newBlock(sp);
                    block->pushStmt(NEWNODE(LetBinding, AST::Pattern(AST::Pattern::TagBind(), sp, rcstringS), TypeRef(sp), NEWNODE(CallMethod, NEWNODE(NamedValue, AST::Path(rcstringF)), AST::PathNode(RcString::newInterned("debug_tuple"), {}), vec$(NEWNODE(String, v.mName.c_str())))));

                    auto sEnt = NEWNODE(NamedValue, AST::Path(rcstringS));
                    makeRefpatA(sp, *block, patsA, e.mItems, [&](size_t idx, auto a) {
                        return NEWNODE(CallMethod, sEnt->clone(), AST::PathNode(rcstringField, {}), vec$(mv$(a)));
                    });
                    block->pushTailExpr(NEWNODE(CallMethod, mv$(sEnt), AST::PathNode(rcstringFinish, {}), {}));
                    code = mkExprnodep(block.release());
                    patA = AST::Pattern(AST::Pattern::TagNamedTuple(), sp, variantPath, mv$(patsA));
                }
                TU_ARMA(Struct, e) {
                    ::std::vector<AST::StructPatternEntry> patsA;
                    auto block = newBlock(sp);
                    block->pushStmt(NEWNODE(LetBinding, AST::Pattern(AST::Pattern::TagBind(), sp, rcstringS), TypeRef(sp), NEWNODE(CallMethod, NEWNODE(NamedValue, AST::Path(rcstringF)), AST::PathNode(RcString::newInterned("debug_struct"), {}), vec$(NEWNODE(String, v.mName.c_str())))));

                    auto sEnt = NEWNODE(NamedValue, AST::Path(rcstringS));
                    makeRefpatA(sp, *block, patsA, e.fields, [&](size_t idx, auto a) {
                        return NEWNODE(CallMethod, sEnt->clone(), AST::PathNode(rcstringField, {}), vec$(NEWNODE(String, e.fields[idx].mName.c_str()), mv$(a)));
                    });
                    block->pushTailExpr(NEWNODE(CallMethod, mv$(sEnt), AST::PathNode(rcstringFinish, {}), {}));

                    code = mkExprnodep(block.release());
                    patA = AST::Pattern(AST::Pattern::TagStruct(), sp, variantPath, mv$(patsA), true);
                }
            }

            ::std::vector< AST::Pattern>    pats;
            pats.push_back( AST::Pattern(AST::Pattern::TagReference(), sp, false, mv$(patA)) );

            arms.push_back(AST::ExprNodeMatchArm(
                mv$(pats),
                {},
                mv$(code)
                ));
        }
        AST::ExprNodeP node = NEWNODE(Match, NEWNODE(NamedValue, AST::Path(rcstringSelfLower)), mv$(arms));

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), mv$(node));
    }
} gDeriveDebug;

// ---- Comparisons

class DeriverInnerCompare: public Deriver {
protected:
    /// Create a final output impl block
    virtual AST::Impl makeRet(Span sp, const RcString& coreName, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> typesToBound, AST::ExprNodeP node) const = 0;
    /// Compare two values, early returning if no more comparisons should happen
    virtual AST::ExprNodeP compareAndRet(Span sp, const RcString& coreName, AST::ExprNodeP v1, AST::ExprNodeP v2) const = 0;
    /// Get the return value for if `compare_and_ret` didn't return early
    virtual AST::ExprNodeP equalValue(Span sp, const RcString& coreName) const = 0;
    /// Get the return value for a mismatch in enum variants
    virtual AST::ExprNodeP enumMismatch(Span sp, const RcString& coreName) const = 0;

public:
    // Struct
    AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Struct& str) const override {
        auto block = newBlock(sp);

        this->iterateStructFields(str, [&](RcString fldName) {
            block->pushStmt(this->compareAndRet(sp, opts.coreName, NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstringSelfLower)), fldName), NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstringV)), fldName)));
        });
        block->pushTailExpr(this->equalValue(sp, opts.coreName));

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), mkExprnodep(block.release()));
    }

    // Enum
    AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Enum& enm) const override {
        AST::Path basePath = *type.mData.as_Path();
        basePath.nodes().back().args() = ::AST::PathParams();
        ::std::vector<AST::ExprNodeMatchArm> arms;

        for (const auto& v : enm.variants()) {
            AST::ExprNodeP code;
            AST::Pattern patA;
            AST::Pattern patB;
            auto variantPath = basePath + v.mName;

            TU_MATCH_HDRA( (v.mData), {)
            TU_ARMA(Unit, e) {
                    code = this->equalValue(sp, opts.coreName);
                    patA = AST::Pattern(AST::Pattern::TagValue(), sp, AST::Pattern::Value::make_Named(variantPath));
                    patB = AST::Pattern(AST::Pattern::TagValue(), sp, AST::Pattern::Value::make_Named(variantPath));
                }
                TU_ARMA(Tuple, e) {
                    auto block = newBlock(sp);
                    ::std::vector<AST::Pattern> patsA;
                    ::std::vector<AST::Pattern> patsB;

                    makeRefpatAb(sp, *block, patsA, patsB, e.mItems, [&](auto idx, auto a, auto b) {
                        return this->compareAndRet(sp, opts.coreName, mv$(a), mv$(b));
                    });
                    block->pushTailExpr(this->equalValue(sp, opts.coreName));

                    patA = AST::Pattern(AST::Pattern::TagNamedTuple(), sp, variantPath, mv$(patsA));
                    patB = AST::Pattern(AST::Pattern::TagNamedTuple(), sp, variantPath, mv$(patsB));
                    code = mkExprnodep(block.release());
                }
                TU_ARMA(Struct, e) {
                    auto block = newBlock(sp);
                    ::std::vector<AST::StructPatternEntry> patsA;
                    ::std::vector<AST::StructPatternEntry> patsB;

                    makeRefpatAb(sp, *block, patsA, patsB, e.fields, [&](const auto& name, auto a, auto b) {
                        return this->compareAndRet(sp, opts.coreName, mv$(a), mv$(b));
                    });
                    block->pushTailExpr(this->equalValue(sp, opts.coreName));

                    patA = AST::Pattern(AST::Pattern::TagStruct(), sp, variantPath, mv$(patsA), true);
                    patB = AST::Pattern(AST::Pattern::TagStruct(), sp, variantPath, mv$(patsB), true);
                    code = mkExprnodep(block.release());
                }
            }

            ::std::vector< AST::Pattern>    pats;
            {
                ::std::vector<AST::Pattern> tuplePats;
                tuplePats.push_back(AST::Pattern(AST::Pattern::TagReference(), sp, false, mv$(patA)));
                tuplePats.push_back(AST::Pattern(AST::Pattern::TagReference(), sp, false, mv$(patB)));
                pats.push_back(AST::Pattern(AST::Pattern::TagTuple(), sp, mv$(tuplePats)));
            }

            arms.push_back(AST::ExprNodeMatchArm(
                mv$(pats),
                {},
                mv$(code)
                ));
        }

        // Default arm
        {
            arms.push_back(AST::ExprNodeMatchArm(::makeVec1(AST::Pattern()), {}, this->enumMismatch(sp, opts.coreName)));
        }

        ::std::vector<AST::ExprNodeP> vals;
        vals.push_back(NEWNODE(NamedValue, AST::Path(rcstringSelfLower)));
        vals.push_back(NEWNODE(NamedValue, AST::Path(rcstringV)));
        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), NEWNODE(Match, NEWNODE(Tuple, mv$(vals)), mv$(arms)));
    }
};

class DeriverPartialEq: public DeriverInnerCompare {
    AST::Impl makeRet(Span sp, const RcString& coreName, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> typesToBound, AST::ExprNodeP node) const override {
        const AST::Path traitPath = getPath(coreName, "cmp", "PartialEq");

        AST::Function fcn(sp, TypeRef(sp, CORETYPE_BOOL), vec$(AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstringSelfLower), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), false, mktypeSelf(sp))), AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstringV), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), false, mktypeSelf(sp)))));
        fcn.setCode(NEWNODE(Block, mv$(node)));

        AST::GenericParams params = getParamsWithBounds(sp, p, traitPath, mv$(typesToBound));

        AST::Impl rv(AST::ImplDef(mv$(params), makeSpanned(sp, traitPath), type.clone()));
        rv.addFunction(sp, {}, AST::Visibility::makeBarePrivate(), false, RcString::newInterned("eq"), mv$(fcn));
        return mv$(rv);
    }

    AST::ExprNodeP compareAndRet(Span sp, const RcString& coreName, AST::ExprNodeP v1, AST::ExprNodeP v2) const override {
        std::vector<AST::ExprNodeIf::Arm> arms;
        arms.push_back(AST::ExprNodeIf::Arm{makeVec1(AST::IfLetCondition{{}, NEWNODE(BinOp, AST::ExprNodeBinOp::CMPNEQU, mv$(v1), mv$(v2))}), NEWNODE(Flow, AST::ExprNodeFlow::RETURN, "", NEWNODE(Bool, false))});
        return NEWNODE(If, std::move(arms), nullptr);
    }

    AST::ExprNodeP equalValue(Span sp, const RcString& coreName) const override {
        return NEWNODE(Bool, true);
    }

    AST::ExprNodeP enumMismatch(Span sp, const RcString& coreName) const override {
        return NEWNODE(Bool, false);
    }

public:
    const char* traitName() const override {
        return "PartialEq";
    }
} gDerivePartialeq;

class DeriverPartialOrd: public DeriverInnerCompare {
    AST::Impl makeRet(Span sp, const RcString& coreName, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> typesToBound, AST::ExprNodeP node) const override {
        const AST::Path traitPath = getPath(coreName, "cmp", "PartialOrd");
        const AST::Path pathOrdering = getPath(coreName, "cmp", "Ordering");

        AST::Path pathOptionOrdering = getPath(coreName, "option", "Option");
        pathOptionOrdering.nodes().back().args().entries.push_back(TypeRef(sp, pathOrdering));

        AST::Function fcn(sp, TypeRef(sp, pathOptionOrdering), vec$(AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstringSelfLower), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), false, mktypeSelf(sp))), AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstringV), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), false, mktypeSelf(sp)))));
        fcn.setCode(NEWNODE(Block, mv$(node)));

        AST::GenericParams params = getParamsWithBounds(sp, p, traitPath, mv$(typesToBound));

        AST::Impl rv(AST::ImplDef(mv$(params), makeSpanned(sp, traitPath), type.clone()));
        rv.addFunction(sp, {}, AST::Visibility::makeBarePrivate(), false, RcString::newInterned("partial_cmp"), mv$(fcn));
        return mv$(rv);
    }

    AST::ExprNodeP compareAndRet(Span sp, const RcString& coreName, AST::ExprNodeP v1, AST::ExprNodeP v2) const override {
        return NEWNODE(Match, NEWNODE(CallPath, getPath(coreName, "cmp", "PartialOrd", "partial_cmp"), ::makeVec2(NEWNODE(UniOp, AST::ExprNodeUniOp::REF, mv$(v1)), NEWNODE(UniOp, AST::ExprNodeUniOp::REF, mv$(v2)))), ::makeVec3(::AST::ExprNodeMatchArm(::makeVec1(AST::Pattern(AST::Pattern::TagValue(), sp, getPath(coreName, "option", "Option", "None"))), {}, NEWNODE(Flow, AST::ExprNodeFlow::RETURN, "", NEWNODE(NamedValue, getPath(coreName, "option", "Option", "None")))), ::AST::ExprNodeMatchArm(::makeVec1(AST::Pattern(AST::Pattern::TagNamedTuple(), sp, getPath(coreName, "option", "Option", "Some"), ::makeVec1(AST::Pattern(AST::Pattern::TagValue(), sp, getPath(coreName, "cmp", "Ordering", "Equal"))))), {}, NEWNODE(Tuple, ::std::vector<AST::ExprNodeP>())), ::AST::ExprNodeMatchArm(::makeVec1(AST::Pattern(AST::Pattern::TagBind(), sp, rcstringRes)), {}, NEWNODE(Flow, AST::ExprNodeFlow::RETURN, "", NEWNODE(NamedValue, AST::Path(rcstringRes))))));
    }

    AST::ExprNodeP equalValue(Span sp, const RcString& coreName) const override {
        return NEWNODE(CallPath, getPath(coreName, "option", "Option", "Some"), ::makeVec1(NEWNODE(NamedValue, getPath(coreName, "cmp", "Ordering", "Equal"))));
    }

    AST::ExprNodeP enumMismatch(Span sp, const RcString& coreName) const override {
        return NEWNODE(CallPath, getPath(coreName, "cmp", "PartialOrd", "partial_cmp"), ::makeVec2(NEWNODE(UniOp, AST::ExprNodeUniOp::REF, NEWNODE(CallPath, getPath(coreName, "intrinsics", "discriminant_value"), makeVec1(NEWNODE(NamedValue, AST::Path(rcstringSelfLower))))), NEWNODE(UniOp, AST::ExprNodeUniOp::REF, NEWNODE(CallPath, getPath(coreName, "intrinsics", "discriminant_value"), makeVec1(NEWNODE(NamedValue, AST::Path(rcstringV)))))));
    }

public:
    const char* traitName() const override {
        return "PartialOrd";
    }
} gDerivePartialord;

class DeriverEq: public Deriver {
    AST::Path getTraitPath(const RcString& coreName) const {
        return getPath(coreName, "cmp", "Eq");
    }

    AST::Impl makeRet(Span sp, const RcString& coreName, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> typesToBound, AST::ExprNodeP node) const {
        const AST::Path traitPath = this->getTraitPath(coreName);

        AST::Function fcn(sp, TypeRef(TypeRef::TagUnit(), sp), vec$(AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstringSelfLower), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), false, mktypeSelf(sp)))));
        fcn.setCode(NEWNODE(Block, mv$(node)));

        AST::GenericParams params = getParamsWithBounds(sp, p, traitPath, mv$(typesToBound));

        AST::Impl rv(AST::ImplDef(mv$(params), makeSpanned(sp, traitPath), type.clone()));
        rv.addFunction(sp, {}, AST::Visibility::makeBarePrivate(), false, rcstringAssertReceiverIsTotalEq, mv$(fcn));
        return mv$(rv);
    }

    AST::ExprNodeP assertIsEq(const AST::Path& methodPath, AST::ExprNodeP val) const {
        return NEWNODE(CallPath, AST::Path(methodPath), vec$(NEWNODE(UniOp, AST::ExprNodeUniOp::REF, mv$(val))));
    }

    AST::ExprNodeP field(const ::std::string& name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstringSelfLower)), RcString::newInterned(name));
    }

    AST::ExprNodeP field(const RcString& name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstringSelfLower)), name);
    }

public:
    const char* traitName() const override {
        return "Eq";
    }

    AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Struct& str) const override {
        const AST::Path assertMethodPath = this->getTraitPath(opts.coreName) + rcstringAssertReceiverIsTotalEq;

        auto block = newBlock(sp);
        this->iterateStructFields(str, [&](RcString name) {
            block->pushStmt(this->assertIsEq(assertMethodPath, this->field(name)));
        });

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), mkExprnodep(block.release()));
    }

    AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Enum& enm) const override {
        const AST::Path assertMethodPath = this->getTraitPath(opts.coreName) + rcstringAssertReceiverIsTotalEq;

        AST::Path basePath = *type.mData.as_Path();
        basePath.nodes().back().args() = ::AST::PathParams();
        ::std::vector<AST::ExprNodeMatchArm> arms;

        for (const auto& v : enm.variants()) {
            AST::ExprNodeP code;
            AST::Pattern patA;
            auto variantPath = basePath + v.mName;

            TU_MATCH_HDRA( (v.mData), {)
            TU_ARMA(Unit, e) {
                    code = NEWNODE(Block);
                    patA = AST::Pattern(AST::Pattern::TagValue(), sp, AST::Pattern::Value::make_Named(variantPath));
                }
                TU_ARMA(Tuple, e) {
                    auto block = newBlock(sp);
                    ::std::vector<AST::Pattern> patsA;
                    makeRefpatA(sp, *block, patsA, e.mItems, [&](size_t idx, auto a) {
                        return this->assertIsEq(assertMethodPath, mv$(a));
                    });

                    patA = AST::Pattern(AST::Pattern::TagNamedTuple(), sp, variantPath, mv$(patsA));
                    code = mkExprnodep(block.release());
                }
                TU_ARMA(Struct, e) {
                    auto block = newBlock(sp);
                    ::std::vector<AST::StructPatternEntry> patsA;
                    makeRefpatA(sp, *block, patsA, e.fields, [&](size_t idx, auto a) {
                        return this->assertIsEq(assertMethodPath, mv$(a));
                    });

                    patA = AST::Pattern(AST::Pattern::TagStruct(), sp, variantPath, mv$(patsA), true);
                    code = mkExprnodep(block.release());
                }
            }

            ::std::vector< AST::Pattern>    pats;
            pats.push_back( AST::Pattern(AST::Pattern::TagReference(), sp, false, mv$(patA)) );

            arms.push_back(AST::ExprNodeMatchArm(
                mv$(pats),
                {},
                mv$(code)
                ));
        }

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), NEWNODE(Match, NEWNODE(NamedValue, AST::Path(rcstringSelfLower)), mv$(arms)));
    }

    AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Union& unn) const override {
        // Eq is just a marker, so it's valid to derive for union
        const AST::Path assertMethodPath = this->getTraitPath(opts.coreName) + rcstringAssertReceiverIsTotalEq;
        auto block = newBlock(sp);

        for (const auto& fld : unn.mVariants) {
            block->pushStmt(this->assertIsEq(assertMethodPath, this->field(fld.mName)));
        }

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(unn), mkExprnodep(block.release()));
    }
} gDeriveEq;

class DeriverOrd: public DeriverInnerCompare {
    AST::Impl makeRet(Span sp, const RcString& coreName, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> typesToBound, AST::ExprNodeP node) const override {
        const AST::Path traitPath = getPath(coreName, "cmp", "Ord");
        const AST::Path pathOrdering = getPath(coreName, "cmp", "Ordering");

        AST::Function fcn(sp, TypeRef(sp, pathOrdering), vec$(AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstringSelfLower), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), false, mktypeSelf(sp))), AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstringV), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), false, mktypeSelf(sp)))));
        fcn.setCode(NEWNODE(Block, mv$(node)));

        AST::GenericParams params = getParamsWithBounds(sp, p, traitPath, mv$(typesToBound));

        AST::Impl rv(AST::ImplDef(mv$(params), makeSpanned(sp, traitPath), type.clone()));
        rv.addFunction(sp, {}, AST::Visibility::makeBarePrivate(), false, RcString::newInterned("cmp"), mv$(fcn));
        return mv$(rv);
    }

    AST::ExprNodeP compareAndRet(Span sp, const RcString& coreName, AST::ExprNodeP v1, AST::ExprNodeP v2) const override {
        return NEWNODE(
            Match,
            NEWNODE(
                CallPath,
                getPath(coreName, "cmp", "Ord", "cmp"),
                // TODO: Optional Ref?
                ::makeVec2(NEWNODE(UniOp, AST::ExprNodeUniOp::REF, mv$(v1)), NEWNODE(UniOp, AST::ExprNodeUniOp::REF, mv$(v2)))
            ),
            ::makeVec2(::AST::ExprNodeMatchArm(::makeVec1(AST::Pattern(AST::Pattern::TagValue(), sp, getPath(coreName, "cmp", "Ordering", "Equal"))), {}, NEWNODE(Tuple, ::std::vector<AST::ExprNodeP>())), ::AST::ExprNodeMatchArm(::makeVec1(AST::Pattern(AST::Pattern::TagBind(), sp, rcstringRes)), {}, NEWNODE(Flow, AST::ExprNodeFlow::RETURN, "", NEWNODE(NamedValue, AST::Path(rcstringRes)))))
        );
    }

    AST::ExprNodeP equalValue(Span sp, const RcString& coreName) const override {
        return NEWNODE(NamedValue, getPath(coreName, "cmp", "Ordering", "Equal"));
    }

    AST::ExprNodeP enumMismatch(Span sp, const RcString& coreName) const override {
        return NEWNODE(CallPath, getPath(coreName, "cmp", "Ord", "cmp"), ::makeVec2(NEWNODE(UniOp, AST::ExprNodeUniOp::REF, NEWNODE(CallPath, getPath(coreName, "intrinsics", "discriminant_value"), makeVec1(NEWNODE(NamedValue, AST::Path(rcstringSelfLower))))), NEWNODE(UniOp, AST::ExprNodeUniOp::REF, NEWNODE(CallPath, getPath(coreName, "intrinsics", "discriminant_value"), makeVec1(NEWNODE(NamedValue, AST::Path(rcstringV)))))));
    }

public:
    const char* traitName() const override {
        return "Ord";
    }
} gDeriveOrd;

class DeriverClone: public Deriver {
    AST::Path getTraitPath(const RcString& coreName) const {
        return AST::Path(coreName, {AST::PathNode(rcstringCloneLower, {}), AST::PathNode(rcstringClone, {})});
    }

    AST::Path getMethodPath(const RcString& coreName) const {
        return getTraitPath(coreName) + rcstringCloneLower;
    }

    AST::Impl makeRet(Span sp, const RcString& coreName, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> typesToBound, AST::ExprNodeP node) const {
        const AST::Path traitPath = this->getTraitPath(coreName);

        AST::Function fcn(sp, mktypeSelf(sp), vec$(AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstringSelfLower), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), false, mktypeSelf(sp)))));
        fcn.setCode(NEWNODE(Block, mv$(node)));

        AST::GenericParams params = getParamsWithBounds(sp, p, traitPath, mv$(typesToBound));

        AST::Impl rv(AST::ImplDef(mv$(params), makeSpanned(sp, traitPath), type.clone()));
        rv.addFunction(sp, {}, AST::Visibility::makeBarePrivate(), false, rcstringCloneLower, mv$(fcn));
        return mv$(rv);
    }

    AST::ExprNodeP cloneValRef(const RcString& coreName, AST::ExprNodeP val) const {
        // TODO: Hack for zero-sized arrays? (Not a 1.19 feature)
        return NEWNODE(CallPath, this->getMethodPath(coreName), vec$(NEWNODE(UniOp, AST::ExprNodeUniOp::REF, mv$(val))));
    }

    AST::ExprNodeP cloneValDirect(const RcString& coreName, AST::ExprNodeP val) const {
        return NEWNODE(CallPath, this->getMethodPath(coreName), vec$(mv$(val)));
    }

    AST::ExprNodeP field(const RcString& name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstringSelfLower)), name);
    }

    AST::ExprNodeP field(const ::std::string& name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstringSelfLower)), RcString::newInterned(name));
    }

public:
    const char* traitName() const override {
        return "Clone";
    }

    AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Struct& str) const override {
        const AST::Path& tyPath = *type.mData.as_Path();

        AST::ExprNodeP node;
        TU_MATCH_HDRA( (str.mData), {)
        TU_ARMA(Unit, e) {
                node = NEWNODE(NamedValue, AST::Path(tyPath));
            }
            TU_ARMA(Struct, e) {
                ::AST::ExprNodeStructLiteral::tValues vals;
                for (const auto& fld : e.ents) {
                    vals.push_back({{}, fld.mName, this->cloneValRef(opts.coreName, this->field(fld.mName))});
                }
                node = NEWNODE(StructLiteral, tyPath, nullptr, mv$(vals));
            }
            TU_ARMA(Tuple, e) {
                ::std::vector<AST::ExprNodeP> vals;
                for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                    vals.push_back(this->cloneValRef(opts.coreName, this->field(FMT(idx))));
                }
                node = NEWNODE(CallPath, AST::Path(tyPath), mv$(vals));
            }
        }

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), NEWNODE(Block, mv$(node)));
    }

    AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Enum& enm) const override {
        AST::Path basePath = *type.mData.as_Path();
        basePath.nodes().back().args() = ::AST::PathParams();
        ::std::vector<AST::ExprNodeMatchArm> arms;

        for (const auto& v : enm.variants()) {
            AST::ExprNodeP code;
            AST::Pattern patA;

            TU_MATCH_HDRA( (v.mData), {)
            TU_ARMA(Unit, e) {
                    code = NEWNODE(NamedValue, basePath + v.mName);
                    patA = AST::Pattern(AST::Pattern::TagValue(), sp, AST::Pattern::Value::make_Named(basePath + v.mName));
                }
                TU_ARMA(Tuple, e) {
                    ::std::vector<AST::Pattern> patsA;
                    ::std::vector<AST::ExprNodeP> nodes;

                    for (size_t idx = 0; idx < e.mItems.size(); idx++) {
                        auto nameA = RcString::newInterned(FMT("a" << idx));
                        patsA.push_back(::AST::Pattern(::AST::Pattern::TagBind(), sp, nameA, ::AST::PatternBinding::Type::REF));
                        nodes.push_back(this->cloneValDirect(opts.coreName, NEWNODE(NamedValue, AST::Path(nameA))));
                    }

                    patA = AST::Pattern(AST::Pattern::TagNamedTuple(), sp, basePath + v.mName, mv$(patsA));
                    code = NEWNODE(CallPath, basePath + v.mName, mv$(nodes));
                }
                TU_ARMA(Struct, e) {
                    ::std::vector<AST::StructPatternEntry> patsA;
                    ::AST::ExprNodeStructLiteral::tValues vals;

                    for (const auto& fld : e.fields) {
                        auto nameA = RcString::newInterned(FMT("a" << fld.mName));
                        patsA.push_back(AST::StructPatternEntry{AST::AttributeList(), fld.mName, ::AST::Pattern(::AST::Pattern::TagBind(), sp, nameA, ::AST::PatternBinding::Type::REF)});
                        vals.push_back({{}, fld.mName, this->cloneValDirect(opts.coreName, NEWNODE(NamedValue, AST::Path(nameA)))});
                    }

                    patA = AST::Pattern(AST::Pattern::TagStruct(), sp, basePath + v.mName, mv$(patsA), true);
                    code = NEWNODE(StructLiteral, basePath + v.mName, nullptr, mv$(vals));
                }
            }

            ::std::vector< AST::Pattern>    pats;
            pats.push_back( AST::Pattern(AST::Pattern::TagReference(), sp, false, mv$(patA)) );

            arms.push_back(AST::ExprNodeMatchArm(
                mv$(pats),
                {},
                mv$(code)
                ));
        }

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), NEWNODE(Match, NEWNODE(NamedValue, AST::Path(rcstringSelfLower)), mv$(arms)));
    }

    AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Union& unn) const override {
        return makeCopyClone(sp, opts, p, type, this->getFieldBounds(unn));
    }

private:
    AST::Impl makeCopyClone(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> fieldBounds) const {
        // Clone on a union can only be a bitwise copy.
        // - This requires a Copy impl. That's up to the user
        auto ret = this->makeRet(sp, opts.coreName, p, type, ::std::move(fieldBounds), NEWNODE(Deref, NEWNODE(NamedValue, AST::Path(rcstringSelfLower))));

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
    AST::Path getTraitPath(const RcString& coreName) const {
        return getPath(coreName, "marker", "Copy");
    }

    AST::Impl makeRet(Span sp, const RcString& coreName, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> typesToBound, AST::ExprNodeP node) const {
        const AST::Path traitPath = this->getTraitPath(coreName);

        AST::GenericParams params = getParamsWithBounds(sp, p, traitPath, mv$(typesToBound));

        AST::Impl rv(AST::ImplDef(mv$(params), makeSpanned(sp, traitPath), type.clone()));
        return mv$(rv);
    }

public:
    const char* traitName() const override {
        return "Copy";
    }

    AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Struct& str) const override {
        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), nullptr);
    }

    AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Enum& enm) const override {
        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), nullptr);
    }

    AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Union& unn) const override {
        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(unn), nullptr);
    }
} gDeriveCopy;

class DeriverDefault: public Deriver {
    AST::Path getTraitPath(const RcString& coreName) const {
        return getPath(coreName, "default", "Default");
    }

    AST::Path getMethodPath(const RcString& coreName) const {
        return AST::Path::newUfcsTrait(::TypeRef(Span()), getTraitPath(coreName), {AST::PathNode(RcString::newInterned("default"), {})});
    }

    AST::Impl makeRet(Span sp, const RcString& coreName, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> typesToBound, AST::ExprNodeP node) const {
        const AST::Path traitPath = this->getTraitPath(coreName);

        AST::Function fcn(sp, mktypeSelf(sp), {});
        fcn.setCode(NEWNODE(Block, mv$(node)));

        AST::GenericParams params = getParamsWithBounds(sp, p, traitPath, mv$(typesToBound));

        AST::Impl rv(AST::ImplDef(mv$(params), makeSpanned(sp, traitPath), type.clone()));
        rv.addFunction(sp, {}, AST::Visibility::makeBarePrivate(), false, RcString::newInterned("default"), mv$(fcn));
        return mv$(rv);
    }

    AST::ExprNodeP defaultCall(const RcString& coreName) const {
        return NEWNODE(CallPath, this->getMethodPath(coreName), {});
    }

public:
    const char* traitName() const override {
        return "Default";
    }

    AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Struct& str) const override {
        const AST::Path& tyPath = *type.mData.as_Path();
        AST::ExprNodeP node;

        TU_MATCH_HDRA( (str.mData), {)
        TU_ARMA(Unit, e) {
                node = NEWNODE(NamedValue, AST::Path(tyPath));
            }
            TU_ARMA(Struct, e) {
                ::AST::ExprNodeStructLiteral::tValues vals;
                bool hasDefault = false;
                for (const auto& fld : e.ents) {
                    if (fld.defaultValue) {
                        hasDefault = true;
                    } else {
                        vals.push_back({{}, fld.mName, this->defaultCall(opts.coreName)});
                    }
                }
                if (hasDefault) {
                    node = NEWNODE(StructLiteralPattern, tyPath, mv$(vals));
                } else {
                    node = NEWNODE(StructLiteral, tyPath, nullptr, mv$(vals));
                }
            }
            TU_ARMA(Tuple, e) {
                ::std::vector<AST::ExprNodeP> vals;
                for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                    vals.push_back(this->defaultCall(opts.coreName));
                }
                node = NEWNODE(CallPath, AST::Path(tyPath), mv$(vals));
            }
        }

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), NEWNODE(Block, mv$(node)));
    }

    AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Enum& enm) const override {
        // 1.74: #[default]
        const AST::EnumVariant* defaultVar = nullptr;
        for (const auto& v : enm.variants()) {
            if (v.mAttrs.has("default")) {
                if (defaultVar) {
                    ERROR(sp, E0000, "Multiple #[default] attributes");
                }
                defaultVar = &v;
            }
        }
        if (!defaultVar) {
            ERROR(sp, E0000, "No #[default] attribute on enum with derive(Default)");
        }

        AST::Path varPath = *type.mData.as_Path() + AST::PathNode(defaultVar->mName);

        ::std::vector<TypeRef> boundTys;
        AST::ExprNodeP node;
        TU_MATCH_HDRA( (defaultVar->mData), { )
        TU_ARMA(Unit, e) {
                node = NEWNODE(NamedValue, std::move(varPath));
            }
            TU_ARMA(Tuple, e) {
                ::std::vector<AST::ExprNodeP> vals;
                for (const auto& fld : e.mItems) {
                    addFieldBoundFromTy(enm.params(), boundTys, fld.mType);
                    vals.push_back(this->defaultCall(opts.coreName));
                }
                node = NEWNODE(CallPath, std::move(varPath), mv$(vals));
            }
            TU_ARMA(Struct, e) {
                ::AST::ExprNodeStructLiteral::tValues vals;
                for (const auto& fld : e.fields) {
                    if (fld.defaultValue) {
                    } else {
                        addFieldBoundFromTy(enm.params(), boundTys, fld.mType);
                        vals.push_back({{}, fld.mName, this->defaultCall(opts.coreName)});
                    }
                }
                node = NEWNODE(StructLiteralPattern, std::move(varPath), mv$(vals));
            }
        }
        return this->makeRet(sp, opts.coreName, p, type, std::move(boundTys), std::move(node));
    }
} gDeriveDefault;

class DeriverHash: public Deriver {
    AST::Path getTraitPath(const RcString& coreName) const {
        return getPath(coreName, "hash", "Hash");
    }

    AST::Path getTraitPathHasher(const RcString& coreName) const {
        return getPath(coreName, "hash", "Hasher");
    }

    AST::Path getMethodPath(const RcString& coreName) const {
        return getTraitPath(coreName) + RcString::newInterned("hash");
    }

    AST::Impl makeRet(Span sp, const RcString& coreName, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> typesToBound, AST::ExprNodeP node) const {
        const AST::Path traitPath = this->getTraitPath(coreName);

        AST::Function fcn(sp, TypeRef(TypeRef::TagUnit(), sp), vec$(AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstringSelfLower), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), false, mktypeSelf(sp))), AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstringState), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), true, TypeRef(sp, rcstringH, 0x100 | 0)))));
        fcn.params().addTyParam(AST::TypeParam(sp, {}, rcstringH));
        fcn.params().addBound(AST::GenericBound::make_IsTrait({sp, {}, TypeRef(sp, rcstringH, 0x100 | 0), {}, this->getTraitPathHasher(coreName)}));
        fcn.setCode(NEWNODE(Block, mv$(node)));

        AST::GenericParams params = getParamsWithBounds(sp, p, traitPath, mv$(typesToBound));

        AST::Impl rv(AST::ImplDef(mv$(params), makeSpanned(sp, traitPath), type.clone()));
        rv.addFunction(sp, {}, AST::Visibility::makeBarePrivate(), false, RcString::newInterned("hash"), mv$(fcn));
        return mv$(rv);
    }

    AST::ExprNodeP hashValRef(const RcString& coreName, AST::ExprNodeP val) const {
        return this->hashValDirect(coreName, NEWNODE(UniOp, AST::ExprNodeUniOp::REF, mv$(val)));
    }

    AST::ExprNodeP hashValDirect(const RcString& coreName, AST::ExprNodeP val) const {
        return NEWNODE(CallPath, this->getMethodPath(coreName), vec$(mv$(val), NEWNODE(NamedValue, AST::Path(rcstringState))));
    }

    AST::ExprNodeP field(const RcString& name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstringSelfLower)), name);
    }

    AST::ExprNodeP field(const std::string& name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstringSelfLower)), RcString::newInterned(name));
    }

public:
    const char* traitName() const override {
        return "Hash";
    }

    AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Struct& str) const override {
        auto block = newBlock(sp);

        TU_MATCH_HDRA( (str.mData), {)
        TU_ARMA(Unit, e) {
            }
            TU_ARMA(Struct, e) {
                for (const auto& fld : e.ents) {
                    block->pushStmt(this->hashValRef(opts.coreName, this->field(fld.mName)));
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

    AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Enum& enm) const override {
        AST::Path basePath = *type.mData.as_Path();
        basePath.nodes().back().args() = ::AST::PathParams();
        ::std::vector<AST::ExprNodeMatchArm> arms;

        for (unsigned int varIdx = 0; varIdx < enm.variants().size(); varIdx++) {
            const auto& v = enm.variants()[varIdx];
            AST::Pattern patA;

            auto varPath = basePath + v.mName;
            auto varIdxHash = enm.variants().size() > 1 ? this->hashValRef(opts.coreName, NEWNODE(Integer, U128(varIdx), CORETYPE_UINT)) : NEWNODE(Tuple, {});

            auto block = newBlock(sp);
            block->pushStmt(mv$(varIdxHash));
            TU_MATCH_HDRA( (v.mData), {)
            TU_ARMA(Unit, e) {
                    patA = AST::Pattern(AST::Pattern::TagValue(), sp, AST::Pattern::Value::make_Named(varPath));
                }
                TU_ARMA(Tuple, e) {
                    ::std::vector<AST::Pattern> patsA;
                    makeRefpatA(sp, *block, patsA, e.mItems, [&](size_t, auto a) {
                        return this->hashValDirect(opts.coreName, mv$(a));
                    });
                    patA = AST::Pattern(AST::Pattern::TagNamedTuple(), sp, varPath, mv$(patsA));
                }
                TU_ARMA(Struct, e) {
                    ::std::vector<AST::StructPatternEntry> patsA;
                    makeRefpatA(sp, *block, patsA, e.fields, [&](size_t, auto a) {
                        return this->hashValDirect(opts.coreName, mv$(a));
                    });
                    patA = AST::Pattern(AST::Pattern::TagStruct(), sp, varPath, mv$(patsA), true);
                }
            }

            ::std::vector< AST::Pattern>    pats;
            pats.push_back( AST::Pattern(AST::Pattern::TagReference(), sp, false, mv$(patA)) );

            arms.push_back(AST::ExprNodeMatchArm(
                mv$(pats),
                {},
                mkExprnodep(block.release())
                ));
        }

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), NEWNODE(Match, NEWNODE(NamedValue, AST::Path(rcstringSelfLower)), mv$(arms)));
    }
} gDeriveHash;

class DeriverRustcEncodable: public Deriver {
    // NOTE: This emits paths like `::rustc_serialize::Encodable` - rustc and crates.io have subtly different crate names
    AST::Path getTraitPath() const {
        return AST::Path(RcString::newInterned("=rustc_serialize"), {AST::PathNode(RcString::newInterned("Encodable"), {})});
    }

    AST::Path getTraitPathEncoder() const {
        return AST::Path(RcString::newInterned("=rustc_serialize"), {AST::PathNode(RcString::newInterned("Encoder"), {})});
    }

    AST::Path getMethodPath() const {
        return getTraitPath() + "encode";
    }

    AST::Impl makeRet(Span sp, const RcString& coreName, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> typesToBound, AST::ExprNodeP node) const {
        const AST::Path traitPath = this->getTraitPath();

        AST::Path resultPath = getPath(coreName, "result", "Result");
        resultPath.nodes()[1].args().entries.push_back(TypeRef(TypeRef::TagUnit(), sp));
        resultPath.nodes()[1].args().entries.push_back(TypeRef(sp, AST::Path::newUfcsTrait(TypeRef(sp, "S", 0x100 | 0), this->getTraitPathEncoder(), {AST::PathNode("Error", {})})));

        AST::Function fcn(sp, TypeRef(sp, mv$(resultPath)), vec$(AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstringSelfLower), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), false, mktypeSelf(sp))), AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstringS), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), true, TypeRef(sp, RcString::newInterned("S"), 0x100 | 0)))));
        fcn.params().addTyParam(AST::TypeParam(sp, {}, "S"));
        fcn.params().addBound(AST::GenericBound::make_IsTrait({sp, {}, TypeRef(sp, "S", 0x100 | 0), {}, this->getTraitPathEncoder()}));
        fcn.setCode(NEWNODE(Block, mv$(node)));

        AST::GenericParams params = getParamsWithBounds(sp, p, traitPath, mv$(typesToBound));

        AST::Impl rv(AST::ImplDef(mv$(params), makeSpanned(sp, traitPath), type.clone()));
        rv.addFunction(sp, {}, AST::Visibility::makeBarePrivate(), false, "encode", mv$(fcn));
        return mv$(rv);
    }

    AST::ExprNodeP encValDirect(AST::ExprNodeP val) const {
        return NEWNODE(CallPath, this->getMethodPath(), vec$(mv$(val), NEWNODE(NamedValue, AST::Path(rcstringS))));
    }

    AST::ExprNodeP encValRef(AST::ExprNodeP val) const {
        return this->encValDirect(NEWNODE(UniOp, AST::ExprNodeUniOp::REF, mv$(val)));
    }

    AST::ExprNodeP field(const RcString& name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstringSelfLower)), name);
    }

    AST::ExprNodeP field(::std::string name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstringSelfLower)), RcString::newInterned(name));
    }

    AST::ExprNodeP encClosure(Span sp, AST::ExprNodeP code) const {
        return NEWNODE(Closure, vec$(::std::make_pair(AST::Pattern(AST::Pattern::TagBind(), sp, rcstringS), ::TypeRef(sp))), ::TypeRef(sp), mv$(code), false, false);
    }

    AST::ExprNodeP getValOk(const RcString& coreName) const {
        return NEWNODE(CallPath, getPath(coreName, "result", "Result", "Ok"), vec$(NEWNODE(Tuple, {})));
    }

public:
    const char* traitName() const override {
        return "RustcEncodable";
    }

    AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Struct& str) const override {
        ::std::string structName = type.mData.as_Path()->nodes().back().name().c_str();

        auto block = newBlock(sp);
        TU_MATCH_HDRA( (str.mData), {)
        TU_ARMA(Unit, e) {
            }
            TU_ARMA(Struct, e) {
                unsigned int idx = 0;
                for (const auto& fld : e.ents) {
                    block->pushStmt(NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_struct_field", vec$(NEWNODE(NamedValue, AST::Path(rcstringS)), NEWNODE(String, fld.mName.c_str()), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->encClosure(sp, this->encValRef(this->field(fld.mName))))));
                    idx++;
                }
            }
            TU_ARMA(Tuple, e) {
                for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                    block->pushStmt(NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_tuple_struct_arg", vec$(NEWNODE(NamedValue, AST::Path(rcstringS)), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->encClosure(sp, this->encValRef(this->field(FMT(idx)))))));
                }
            }
        }

        block->pushTailExpr( this->getValOk(opts.coreName) );
        auto closure = this->encClosure( sp, mkExprnodep(block.release()) );

        ::AST::ExprNodeP    node;
        TU_MATCH_HDRA( (str.mData), {)
        TU_ARMA(Unit, e) {
                node = getValOk(opts.coreName);
            }
            TU_ARMA(Struct, e) {
                node = NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_struct", vec$(NEWNODE(NamedValue, AST::Path(rcstringS)), NEWNODE(String, structName), NEWNODE(Integer, U128(e.ents.size()), CORETYPE_UINT), mv$(closure)));
            }
            TU_ARMA(Tuple, e) {
                node = NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_tuple_struct", vec$(NEWNODE(NamedValue, AST::Path(rcstringS)), NEWNODE(String, structName), NEWNODE(Integer, U128(e.ents.size()), CORETYPE_UINT), mv$(closure)));
            }
        }

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), mv$(node));
    }

    AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Enum& enm) const override {
        AST::Path basePath = *type.mData.as_Path();
        basePath.nodes().back().args() = ::AST::PathParams();
        ::std::vector<AST::ExprNodeMatchArm> arms;

        auto sEnt = NEWNODE(NamedValue, AST::Path(rcstringS));

        for (unsigned int varIdx = 0; varIdx < enm.variants().size(); varIdx++) {
            const auto& v = enm.variants()[varIdx];
            AST::ExprNodeP code;
            AST::Pattern patA;

            TU_MATCH_HDRA((v.mData), {)
            TU_ARMA(Unit, e) {
                    code = NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_enum_variant", vec$(sEnt->clone(), NEWNODE(String, v.mName.c_str()), NEWNODE(Integer, U128(varIdx), CORETYPE_UINT), NEWNODE(Integer, U128(0), CORETYPE_UINT), this->encClosure(sp, this->getValOk(opts.coreName))));
                    patA = AST::Pattern(AST::Pattern::TagValue(), sp, AST::Pattern::Value::make_Named(basePath + v.mName));
                }
                TU_ARMA(Tuple, e) {
                    auto block = newBlock(sp);
                    ::std::vector<AST::Pattern> patsA;
                    makeRefpatA(sp, *block, patsA, e.mItems, [&](size_t idx, auto a) {
                        return NEWNODE(CallPath, this->getTraitPathEncoder() + RcString::newInterned("emit_enum_variant_arg"), vec$(sEnt->clone(), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->encClosure(sp, this->encValDirect(mv$(a)))));
                    });
                    block->pushTailExpr(this->getValOk(opts.coreName));

                    code = NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_enum_variant", vec$(sEnt->clone(), NEWNODE(String, v.mName.c_str()), NEWNODE(Integer, U128(varIdx), CORETYPE_UINT), NEWNODE(Integer, U128(e.mItems.size()), CORETYPE_UINT), this->encClosure(sp, mkExprnodep(block.release()))));
                    patA = AST::Pattern(AST::Pattern::TagNamedTuple(), sp, basePath + v.mName, mv$(patsA));
                }
                TU_ARMA(Struct, e) {
                    auto block = newBlock(sp);
                    ::std::vector<AST::StructPatternEntry> patsA;
                    makeRefpatA(sp, *block, patsA, e.fields, [&](size_t idx, auto a) {
                        return NEWNODE(CallPath, this->getTraitPathEncoder() + RcString::newInterned("emit_enum_struct_variant_field"), vec$(sEnt->clone(), NEWNODE(String, e.fields[idx].mName.c_str()), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->encClosure(sp, this->encValDirect(mv$(a)))));
                    });
                    block->pushTailExpr(this->getValOk(opts.coreName));

                    patA = AST::Pattern(AST::Pattern::TagStruct(), sp, basePath + v.mName, mv$(patsA), true);
                    code = NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_enum_struct_variant", vec$(sEnt->clone(), NEWNODE(String, v.mName.c_str()), NEWNODE(Integer, U128(varIdx), CORETYPE_UINT), NEWNODE(Integer, U128(e.fields.size()), CORETYPE_UINT), this->encClosure(sp, mkExprnodep(block.release()))));
                }
            }

            ::std::vector< AST::Pattern>    pats;
            pats.push_back( AST::Pattern(AST::Pattern::TagReference(), sp, false, mv$(patA)) );

            arms.push_back(AST::ExprNodeMatchArm(
                mv$(pats),
                {},
                mv$(code)
                ));
        }

        auto nodeMatch = NEWNODE(Match, NEWNODE(NamedValue, AST::Path(rcstringSelfLower)), mv$(arms));

        ::std::string enumName = type.mData.as_Path()->nodes().back().name().c_str();
        auto node = NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_enum", vec$(mv$(sEnt), NEWNODE(String, enumName), this->encClosure(sp, mv$(nodeMatch))));

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), mv$(node));
    }
} gDeriveRustcEncodable;

class DeriverRustcDecodable: public Deriver {
    // NOTE: This emits paths like `::rustc_serialize::Encodable` - rustc and crates.io have subtly different crate names
    AST::Path getTraitPath() const {
        return AST::Path(RcString::newInterned("=rustc_serialize"), {AST::PathNode(RcString::newInterned("Decodable"), {})});
    }

    AST::Path getTraitPathDecoder() const {
        return AST::Path(RcString::newInterned("=rustc_serialize"), {AST::PathNode(RcString::newInterned("Decoder"), {})});
    }

    AST::Path getMethodPath() const {
        return getTraitPath() + "decode";
    }

    AST::Impl makeRet(Span sp, const RcString& coreName, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> typesToBound, AST::ExprNodeP node) const {
        const AST::Path traitPath = this->getTraitPath();

        AST::Path resultPath = getPath(coreName, "result", "Result");
        resultPath.nodes()[1].args().entries.push_back(mktypeSelf(sp));
        resultPath.nodes()[1].args().entries.push_back(TypeRef(sp, AST::Path::newUfcsTrait(TypeRef(sp, "D", 0x100 | 0), this->getTraitPathDecoder(), {AST::PathNode("Error", {})})));

        AST::Function fcn(
            sp,
            TypeRef(sp, resultPath),
            vec$(
                //AST::Function::Arg( AST::Pattern(AST::Pattern::TagBind(), sp, rcstring_self), TypeRef(TypeRef::TagReference(), sp, false, AST::LifetimeRef(), mktype_Self(sp)) ),
                AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, "d"), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), true, TypeRef(sp, "D", 0x100 | 0)))
            )
        );
        fcn.params().addTyParam(AST::TypeParam(sp, {}, "D"));
        fcn.params().addBound(AST::GenericBound::make_IsTrait({sp, {}, TypeRef(sp, "D", 0x100 | 0), {}, this->getTraitPathDecoder()}));
        fcn.setCode(NEWNODE(Block, mv$(node)));

        AST::GenericParams params = getParamsWithBounds(sp, p, traitPath, mv$(typesToBound));

        AST::Impl rv(AST::ImplDef(mv$(params), makeSpanned(sp, traitPath), type.clone()));
        rv.addFunction(sp, {}, AST::Visibility::makeBarePrivate(), false, "decode", mv$(fcn));
        return mv$(rv);
    }

    AST::ExprNodeP decVal() const {
        return NEWNODE(CallPath, this->getMethodPath(), vec$(NEWNODE(NamedValue, AST::Path("d"))));
    }

    AST::ExprNodeP field(const ::std::string& name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstringSelfLower)), RcString::newInterned(name));
    }

    AST::ExprNodeP decClosure(Span sp, AST::ExprNodeP code) const {
        return NEWNODE(Closure, vec$(::std::make_pair(AST::Pattern(AST::Pattern::TagBind(), sp, "d"), ::TypeRef(sp))), ::TypeRef(sp), mv$(code), false, false);
    }

    AST::ExprNodeP getValErrStr(const RcString& coreName, ::std::string errStr) const {
        return NEWNODE(CallPath, getPath(coreName, "result", "Result", "Err"), vec$(NEWNODE(CallMethod, NEWNODE(NamedValue, AST::Path("d")), AST::PathNode("error"), vec$(NEWNODE(String, errStr)))));
    }

    AST::ExprNodeP getValOk(const RcString& coreName, AST::ExprNodeP inner) const {
        return NEWNODE(CallPath, getPath(coreName, "result", "Result", "Ok"), vec$(mv$(inner)));
    }

    AST::ExprNodeP getValOkUnit(const RcString& coreName) const {
        return getValOk(coreName, NEWNODE(Tuple, {}));
    }

public:
    const char* traitName() const override {
        return "RustcDecodable";
    }

    AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Struct& str) const override {
        AST::Path basePath = *type.mData.as_Path();
        ::std::string structName = basePath.nodes().back().name().c_str();

        AST::ExprNodeP nodeV;
        TU_MATCH_HDRA((str.mData), {)
        TU_ARMA(Unit, e) {
            }
            TU_ARMA(Struct, e) {
                ::AST::ExprNodeStructLiteral::tValues vals;
                unsigned int idx = 0;
                for (const auto& fld : e.ents) {
                    vals.push_back({{}, fld.mName, NEWNODE(UniOp, ::AST::ExprNodeUniOp::QMARK, NEWNODE(CallPath, this->getTraitPathDecoder() + "read_struct_field", vec$(NEWNODE(NamedValue, AST::Path("d")), NEWNODE(String, fld.mName.c_str()), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->decClosure(sp, this->decVal()))))});
                    idx++;
                }
                nodeV = NEWNODE(StructLiteral, basePath, nullptr, mv$(vals));
            }
            TU_ARMA(Tuple, e) {
                ::std::vector<AST::ExprNodeP> vals;
                for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                    vals.push_back(NEWNODE(UniOp, ::AST::ExprNodeUniOp::QMARK, NEWNODE(CallPath, this->getTraitPathDecoder() + "read_tuple_struct_arg", vec$(NEWNODE(NamedValue, AST::Path("d")), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->decClosure(sp, this->decVal())))));
                }
                nodeV = NEWNODE(CallPath, mv$(basePath), mv$(vals));
            }
        }

        auto closure = this->decClosure( sp, this->getValOk(opts.coreName, mv$(nodeV)) );

        auto args = vec$( NEWNODE(NamedValue, AST::Path("d")), NEWNODE(String, structName), AST::ExprNodeP(), mv$(closure) );

        ::AST::ExprNodeP    node;
        TU_MATCH_HDRA((str.mData), {)
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

    AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Enum& enm) const override {
        AST::Path basePath = *type.mData.as_Path();
        basePath.nodes().back().args() = ::AST::PathParams();
        ::std::vector<AST::ExprNodeMatchArm> arms;

        // 1. Variant names
        ::std::vector<AST::ExprNodeP> varNameStrs;

        // 2. Decoding arms
        for (unsigned int varIdx = 0; varIdx < enm.variants().size(); varIdx++) {
            const auto& v = enm.variants()[varIdx];
            AST::ExprNodeP code;

            TU_MATCH_HDRA( (v.mData), {)
            TU_ARMA(Unit, e) {
                    code = NEWNODE(NamedValue, basePath + v.mName);
                }
                TU_ARMA(Tuple, e) {
                    ::std::vector<AST::ExprNodeP> args;

                    for (unsigned int idx = 0; idx < e.mItems.size(); idx++) {
                        args.push_back(NEWNODE(UniOp, ::AST::ExprNodeUniOp::QMARK, NEWNODE(CallPath, this->getTraitPathDecoder() + "read_enum_variant_arg", vec$(NEWNODE(NamedValue, AST::Path("d")), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->decClosure(sp, this->decVal())))));
                    }
                    code = NEWNODE(CallPath, basePath + v.mName, mv$(args));
                }
                TU_ARMA(Struct, e) {
                    ::AST::ExprNodeStructLiteral::tValues vals;

                    unsigned int idx = 0;
                    for (const auto& fld : e.fields) {
                        vals.push_back({{}, fld.mName, NEWNODE(UniOp, ::AST::ExprNodeUniOp::QMARK, NEWNODE(CallPath, this->getTraitPathDecoder() + "read_enum_struct_variant_field", vec$(NEWNODE(NamedValue, AST::Path("d")), NEWNODE(String, fld.mName.c_str()), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->decClosure(sp, this->decVal()))))});
                        idx++;
                    }

                    code = NEWNODE(StructLiteral, basePath + v.mName, nullptr, mv$(vals));
                }
            }

            ::std::vector< AST::Pattern>    pats;
            pats.push_back( AST::Pattern(AST::Pattern::TagValue(), sp, AST::Pattern::Value::make_Integer({CORETYPE_UINT, U128(varIdx)})) );

            arms.push_back(AST::ExprNodeMatchArm(
                mv$(pats),
                {},
                this->getValOk(opts.coreName, mv$(code))
                ));
            varNameStrs.push_back( NEWNODE(String, v.mName.c_str()) );
        }

        // Default arm
        {
            arms.push_back(AST::ExprNodeMatchArm(::makeVec1(AST::Pattern()), {}, this->getValErrStr(opts.coreName, "enum value unknown")));
        }

        auto nodeMatch = NEWNODE(Match, NEWNODE(NamedValue, AST::Path("idx")), mv$(arms));
        auto nodeVarClosure = NEWNODE(Closure, vec$(::std::make_pair(AST::Pattern(AST::Pattern::TagBind(), sp, "d"), ::TypeRef(sp)), ::std::make_pair(AST::Pattern(AST::Pattern::TagBind(), sp, "idx"), ::TypeRef(sp))), ::TypeRef(sp), mv$(nodeMatch), false, false);
        ::std::string enumName = type.mData.as_Path()->nodes().back().name().c_str();

        auto nodeRev = NEWNODE(CallPath, this->getTraitPathDecoder() + "read_enum_variant", vec$(NEWNODE(NamedValue, AST::Path("d")), NEWNODE(UniOp, AST::ExprNodeUniOp::REF, NEWNODE(Array, mv$(varNameStrs))), mv$(nodeVarClosure)));

        auto node = NEWNODE(CallPath, this->getTraitPathDecoder() + "read_enum", vec$(NEWNODE(NamedValue, AST::Path("d")), NEWNODE(String, enumName), this->decClosure(sp, mv$(nodeRev))));

        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), mv$(node));
    }
} gDeriveRustcDecodable;

class DeriverConstParamTy: public Deriver {
    AST::Impl handleGeneric(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> typesToBound) const {
        const AST::Path traitPath = getPath(opts.coreName, "marker", "StructuralPartialEq");
        AST::GenericParams params = getParamsWithBounds(sp, p, traitPath, mv$(typesToBound));
        AST::Impl rv(AST::ImplDef(mv$(params), makeSpanned(sp, traitPath), type.clone()));
        return mv$(rv);
    }

public:
    const char* traitName() const override {
        return "ConstParamTy";
    }

    AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Struct& str) const override {
        return handleGeneric(sp, opts, p, type, this->getFieldBounds(str));
    }

    AST::Impl handleItem(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Enum& enm) const override {
        return handleGeneric(sp, opts, p, type, this->getFieldBounds(enm));
    }
} gDeriveConstParamTy;

// --------------------------------------------------------------------
// Select and dispatch the correct derive() handler
// --------------------------------------------------------------------
static const Deriver* findImpl(const RcString& traitName) {
#define _(obj)                          \
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
#undef _
    return nullptr;
}

namespace {
    std::vector<AST::Path> getDeriveItems(const AST::Attribute& attr) {
        Token tok;
        std::vector<AST::Path> rv;

        TTStream lex(attr.span(), ParseState(), attr.data());
        lex.getTokenCheck(TOK_PAREN_OPEN);
        while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
            if (lex.getTokenIf(TOK_DOUBLE_COLON)) {
                auto item = lex.lookahead(0) == TOK_STRING ? AST::Path(lex.getTokenCheck(TOK_STRING).str().c_str(), {}) : AST::Path((std::string("=") + lex.getTokenCheck(TOK_IDENT).ident().name.c_str()).c_str(), {});
                lex.getTokenCheck(TOK_DOUBLE_COLON);
                do {
                    item += AST::PathNode(lex.getTokenCheck(TOK_IDENT).ident().name);
                } while (lex.getTokenIf(TOK_DOUBLE_COLON));
                rv.push_back(std::move(item));
            } else if (lex.getTokenIf(TOK_INTERPOLATED_TYPE, tok)) {
                const auto& ty = tok.fragType();
                ASSERT_BUG(lex.pointSpan(), ty.isPath(), "TODO: No path :ty in derive, " << ty);
                ASSERT_BUG(lex.pointSpan(), ty.mData.as_Path(), "" << ty);
                rv.push_back(*ty.mData.as_Path());
            } else {
                auto item = AST::Path::newRelative({}, {});
                do {
                    item += AST::PathNode(lex.getTokenCheck(TOK_IDENT).ident().name);
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

    TypeRef makeType(const Span& sp, const AST::AbsolutePath& path, const AST::GenericParams& params) {
        TypeRef type(sp, path);
        auto& typesArgs = type.path().nodes().back().args();
        for (const auto& param : params.mParams) {
            if (const auto* pe = param.opt_Type()) {
                typesArgs.entries.push_back(TypeRef(TypeRef::TagArg(), sp, pe->name()));
            }
            if (const auto* pe = param.opt_Value()) {
                auto p = AST::Path(pe->name().name);
                typesArgs.entries.push_back(AST::ExprNodeP(new AST::ExprNodeNamedValue(std::move(p))));
            }
        }
        return type;
    }

    std::vector<RcString> findMacro(const Span& sp, const AST::Crate& crate, const AST::Module& mod, const AST::Path& traitPath) {
        std::vector<RcString> macPath;

        if (traitPath.is_trivial()) {
            //auto mac_name = RcString::new_interned( FMT("derive#" << trait.name().elems.back()) );
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
            auto mac = ExpandLookupMacro(sp, crate, LList<const AST::Module*>(nullptr, &mod), traitPath);

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
static void deriveItem(const Span& sp, const AST::Crate& crate, AST::Module& mod, const AST::Attribute& attr, const AST::AbsolutePath& path, slice<const AST::Attribute> attrs, const AST::Visibility& vis, const T& item) {
    auto deriveItems = getDeriveItems(attr);
    if (deriveItems.empty()) {
        //ERROR(sp, E0000, "#[derive()] requires a list of known traits to derive");
        return;
    }

    DEBUG("path = " << path);

    auto type = makeType(sp, path, item.params());

    DeriveOpts opts = {crate.extCratenameCore};

    ::std::vector<AST::Path> missingHandlers;
    for (const auto& traitPath : deriveItems) {
        DEBUG("- " << traitPath);

        if (traitPath.is_trivial()) {
            auto dp = findImpl(traitPath.asTrivial());
            if (dp) {
                mod.addItem(sp, AST::Visibility::makeBarePrivate(), "", dp->handleItem(sp, opts, item.params(), type, item), {});
                continue;
            }
        }

        // TODO: Handle full paths to standard library traits

        std::vector<RcString> macPath = findMacro(sp, crate, mod, traitPath);
        if (!macPath.empty()) {
            auto lex = ProcMacroInvoke(sp, crate, macPath, attrs, vis, path.nodes.back(), item);
            if (lex) {
                lex->parseState().module = &mod;
                ParseModRootItems(*lex, mod);
            } else {
                ERROR(sp, E0000, "proc_macro derive failed");
            }
            continue;
        }

        // HACK! If the trait path is for `=core` and the last component passes `find_impl`, then assume it's a proper path
        // Some crates spell builtin derives as fully-qualified `::core` paths.

        // Absolute path
        if (const auto* ap = traitPath.cls.opt_Absolute()) {
            // For `::core` (encoded as `=core` due to how it's parsed in `get_derive_items`)
            if (ap->crate == "=core") {
                // And if the last node (ignore intermediate nodes) returns a valid builtin
                if (auto dp = findImpl(ap->nodes.back().name())) {
                    // Use that
                    mod.addItem(sp, AST::Visibility::makeBarePrivate(), "", dp->handleItem(sp, opts, item.params(), type, item), {});
                    continue;
                }
            }
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

    void handle(const Span& sp, const AST::Attribute& attr, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t modIdx, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        TU_MATCH_DEF(
            ::AST::Item,
            (i),
            (e),
            (TODO(sp, "Handle #[derive] for other item types - " << i.tagStr());),
            (
                None,
                // Ignore, it's been deleted
            ),
            (Union, deriveItem(sp, crate, mod, attr, path, attrs, vis, e);),
            (Enum, deriveItem(sp, crate, mod, attr, path, attrs, vis, e);),
            (Struct, deriveItem(sp, crate, mod, attr, path, attrs, vis, e);)
        )
    }
};

STATIC_DECORATOR("derive", DecoratorDerive)

// TODO: `derive_const` should generate const impls, but mrustc doesn't care
class DecoratorDeriveConst: public DecoratorDerive {};
STATIC_DECORATOR("derive_const", DecoratorDeriveConst)


class CDocHandler: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, AST::Impl& impl, const RcString& name, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, const AST::AbsolutePath& path, AST::Trait& trait, slice<const AST::Attribute> attrs, AST::Item& i) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::StructItem& si) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::TupleItem& si) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::EnumVariant& ev) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::ExprNodeMatchArm& expr) const override {
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
    typedef void (*cbT)(const Span& sp, AST::Crate& crate, const std::string&, const AST::AbsolutePath&);
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

void handleSave(const Span& sp, AST::Crate& crate, const std::string& name, const AST::AbsolutePath& path) {
    auto rv = crate.mLangItems.insert(::std::make_pair(name, path));
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

void handleLangItem(const Span& sp, AST::Crate& crate, const AST::AbsolutePath& path, const ::std::string& name, eItemType type, AST::Item& item) {
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

        H::add("iterator", Handler(ITEM_TRAIT, handleSave));    /* mrustc just desugars? */
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
    else if (name == "unsafe_cell") {
    } else if (name == "alloc_layout") {
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
    // - i128/u128 helpers (not used by mrustc)
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

    auto rv = crate.mLangItems.insert(::std::make_pair(realName == nullptr ? name : realName, path));
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

    void handle(const Span& sp, const AST::Attribute& attr, AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        auto name = attr.parseEqualsString(crate, mod);
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
                // - mrustc is lazy and inefficient, so these don't matter :)
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

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, const AST::AbsolutePath& path, AST::Trait& trait, slice<const AST::Attribute> attrs, AST::Item& i) const override {
        // TODO: Trait ATYs (a sub-item of others)
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::EnumVariant& ev) const override {
        // TODO: Enum variants (sub-item of other lang items)
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, AST::Impl& impl, const RcString& name, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        // TODO: lang items on associated items (e.g. functions - `RangeFull::new`)
    }
};

class DecoratorMain: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const AST::Attribute& attr, AST::Crate& crate, const AST::AbsolutePath& path, AST::Module&, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        if (i.is_None()) {
            // Ignore.
        } else if (/*const auto* e =*/i.opt_Function()) {
            auto rv = crate.mLangItems.insert(::std::make_pair(::std::string("mrustc-main"), path));
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

    void handle(const Span& sp, const AST::Attribute& attr, AST::Crate& crate, const AST::AbsolutePath& path, AST::Module&, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        if (i.is_None()) {
        } else if (i.is_Function()) {
            auto rv = crate.mLangItems.insert(::std::make_pair(::std::string("mrustc-start"), path));
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

    void handle(const Span& sp, const AST::Attribute& attr, AST::Crate& crate, const AST::AbsolutePath& path, AST::Module&, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        if (i.is_Function()) {
            auto rv = crate.mLangItems.insert(::std::make_pair(::std::string("mrustc-panic_implementation"), path));
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

    void handle(const Span& sp, const AST::Attribute& attr, AST::Crate& crate, const AST::AbsolutePath& path, AST::Module&, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        if (i.is_Function()) {
            auto rv = crate.mLangItems.insert(::std::make_pair(::std::string("mrustc-panic_implementation"), path));
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

    void handle(const Span& sp, const AST::Attribute& attr, AST::Crate& crate, const AST::AbsolutePath& path, AST::Module&, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        // Attribute that acts as like `#[no_mangle]` `#[linkage="external"]`
    }
};

class DecoratorAllocErrorHandler: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const AST::Attribute& attr, AST::Crate& crate, const AST::AbsolutePath& path, AST::Module&, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        if (i.is_Function()) {
            auto rv = crate.mLangItems.insert(::std::make_pair(::std::string("mrustc-alloc_error_handler"), path));
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

    void handle(const Span& sp, const AST::Attribute&, AST::Crate& crate, const AST::AbsolutePath& path, AST::Module&, size_t, slice<const AST::Attribute>, const AST::Visibility&, AST::Item& item) const override {
        if (!item.is_Static()) {
            ERROR(sp, E0000, "#[global_allocator] on non-static " << path);
        }
        auto rv = crate.mLangItems.insert(::std::make_pair(::std::string("mrustc-global_allocator"), path));
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
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, AST::Impl& impl, const RcString& name, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, const AST::AbsolutePath& path, AST::Trait& trait, slice<const AST::Attribute> attrs, AST::Item& i) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::StructItem& si) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::TupleItem& si) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::EnumVariant& ev) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::ExprNodeP& expr) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::ExprNodeMatchArm& expr) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::ExprNodeStructLiteral::Ent& expr) const override {
    }
};

class CHandlerAllow: public CMultiHandlerLint {};

STATIC_DECORATOR("allow", CHandlerAllow);

class CHandlerWarn: public CMultiHandlerLint {};

STATIC_DECORATOR("warn", CHandlerWarn);

class CHandlerDeny: public CMultiHandlerLint {};

STATIC_DECORATOR("deny", CHandlerDeny);

class CHandlerForbid: public CMultiHandlerLint {};

STATIC_DECORATOR("forbid", CHandlerForbid);


// #[must_use] - Marks a type needing to be consumed
class CHandlerMustUse: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        // TODO: only allowed on types
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, AST::Impl& impl, const RcString& name, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        // TODO: only allowed on associated types
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, const AST::AbsolutePath& path, AST::Trait& trait, slice<const AST::Attribute> attrs, AST::Item& i) const override {
        // TODO: only allowed on associated types
    }
};

STATIC_DECORATOR("must_use", CHandlerMustUse);

// #[non_exhaustive] - Tag an enum as being extensible
class CHandlerNonExhaustive: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        // TODO: only allowed on types
    }
};

STATIC_DECORATOR("non_exhaustive", CHandlerNonExhaustive);

// #[path] - Already used by this stage
class CHandlerPath: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        // TODO: only allowed on modules
    }
};

STATIC_DECORATOR("path", CHandlerPath);

// #[rustc_promotable] - ?
class CHandlerRustcPromotable: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, AST::Impl& impl, const RcString& name, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        // TODO: only allowed on functions?
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, const AST::AbsolutePath& path, AST::Trait& trait, slice<const AST::Attribute> attrs, AST::Item& i) const override {
        // TODO: only allowed on functions?
    }
};

STATIC_DECORATOR("rustc_promotable", CHandlerRustcPromotable);

// #[rustc_inherit_overflow_checks]
class CHandlerRustcInheritOverflowChecks: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, AST::Impl& impl, const RcString& name, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, const AST::AbsolutePath& path, AST::Trait& trait, slice<const AST::Attribute> attrs, AST::Item& i) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::ExprNodeP& expr) const override {
    }
};

STATIC_DECORATOR("rustc_inherit_overflow_checks", CHandlerRustcInheritOverflowChecks);

// #[rustc_on_unimplemented]
class CHandlerRustcOnUnimiplemented: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        // Trait only.
    }
};

STATIC_DECORATOR("rustc_on_unimplemented", CHandlerRustcOnUnimiplemented);


// #[rustc_box] - Marks the `Box::new` inner constructor
class CHandlerRustBox: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::ExprNodeP& expr) const override {
        auto* n = cast<AST::ExprNodeCallPath>(expr.get());
        ASSERT_BUG(expr->span(), n, "");
        ASSERT_BUG(expr->span(), n->mArgs.size() == 1, "");
        auto val = std::move(n->mArgs[0]);
        auto span = n->span();
        expr.reset(new AST::ExprNodeUniOp(AST::ExprNodeUniOp::BOX, std::move(val)));
        expr->setSpan(span);
    }
};

STATIC_DECORATOR("rustc_box", CHandlerRustBox);


class CMultiHandlerStability: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, AST::Impl& impl, const RcString& name, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, const AST::AbsolutePath& path, AST::Trait& trait, slice<const AST::Attribute> attrs, AST::Item& i) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::StructItem& si) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::TupleItem& si) const override {
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::EnumVariant& ev) const override {
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

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
    }
};

STATIC_DECORATOR("allow_internal_unstable", CHandlerAllowInternalUnstable);


class DecoratorNoStd: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate) const override {
        if (crate.loadStd != AST::Crate::LOAD_STD && crate.loadStd != AST::Crate::LOAD_CORE) {
            WARNING(sp, W0000, "Use of #![no_std] with itself or #![no_core]");
            return;
        }
        crate.loadStd = AST::Crate::LOAD_CORE;
    }
};

class DecoratorNoCore: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate) const override {
        if (crate.loadStd != AST::Crate::LOAD_STD && crate.loadStd != AST::Crate::LOAD_NONE) {
            WARNING(sp, W0000, "Use of #![no_core] with itself or #![no_std]");
        }
        crate.loadStd = AST::Crate::LOAD_NONE;
    }
};

class DecoratorNoMain: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span&, const AST::Attribute&, AST::Crate& crate) const override {
        crate.noMain = true;
    }
};

//class Decorator_Prelude:
//    public ExpandDecorator
//{
//public:
//    AttrStage stage() const override { return AttrStage::Pre; }
//};

class DecoratorNoPrelude: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module&, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
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

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module&, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
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
            if (p.is_relative()) {
                crate.preludePath = AST::Path(path);
                crate.preludePath.nodes().pop_back();
                crate.preludePath += p;
            } else {
                crate.preludePath = AST::Path(p);
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
    //Register_Synext_Decorator_G<Decorator_Prelude>("prelude");
    RegisterSynextDecoratorG<DecoratorPreludeImport>("prelude_import");
    RegisterSynextDecoratorG<DecoratorNoPrelude>("no_prelude");
}


class CTestHandler: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    } // Expand early so tests are removed before inner expansion

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module&, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        if (!i.is_Function()) {
            ERROR(sp, E0000, "#[test] can only be put on functions - found on " << i.tagStr());
        }

        if (crate.testHarness) {
            ::AST::TestDesc td;
            td.span = sp;
            for (const auto& node : path.nodes) {
                td.name += "::";
                td.name += node.c_str();
            }
            td.path = path;

            crate.tests.push_back(mv$(td));
        } else {
            i = AST::Item::make_None({});
        }
    }
};

class CTestHandlerSP: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
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
                    td.panicType = ::AST::TestDesc::ShouldPanic::YesWithMessage;

                    TTStream lex(sp, ParseState(), mi.data());
                    auto parseMessage = [&]() {
                        auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
                        if (auto* v = cast<::AST::ExprNodeString>(&*n)) {
                            td.expectedPanicMessage = v->mValue;
                        } else {
                            throw ParseError::Unexpected(lex, Token(InterpolatedFragment(InterpolatedFragment::EXPR, n.release())), TOK_STRING);
                        }
                    };
                    if (lex.getTokenIf(TOK_EQUAL)) {
                        parseMessage();
                    } else {
                        lex.getTokenCheck(TOK_PAREN_OPEN);
                        while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
                            auto n = lex.getTokenCheck(TOK_IDENT).ident().name;
                            if (n == "expected") {
                                lex.getTokenCheck(TOK_EQUAL);
                                parseMessage();
                            } else {
                                TODO(sp, "Handle #[should_panic(" << n << ")");
                            }
                            if (!lex.getTokenIf(TOK_COMMA)) {
                                break;
                            }
                        }
                        lex.getTokenCheck(TOK_PAREN_CLOSE);
                    }
                } else {
                    td.panicType = ::AST::TestDesc::ShouldPanic::Yes;
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

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module&, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
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
    , def(::std::move(def)) {
    RegisterSynextDecoratorStatic(this);
}
