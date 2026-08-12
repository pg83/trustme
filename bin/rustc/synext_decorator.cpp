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
                fcn.markings.inline_type = AST::Function::Markings::Inline::Never;
            } else if (attr == "always") {
                fcn.markings.inline_type = AST::Function::Markings::Inline::Always;
            } else {
                ERROR(lex.point_span(), E0000, "Unknown inline type #[inline(" << attr << ")]");
            }
            lex.getTokenCheck(TOK_PAREN_CLOSE);
            lex.getTokenCheck(TOK_EOF);
        } else {
            fcn.markings.inline_type = AST::Function::Markings::Inline::Normal;
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
        fcn.markings.is_cold = true;
    }
};

STATIC_DECORATOR("cold", CHandlerCold);

class CHandlerRustcLegacyConstGenerics: public CommonFunction {
    void handle(const AST::Attribute& mi, AST::Function& fcn) const override {
        TTStream lex(mi.span(), ParseState(), mi.data());
        lex.getTokenCheck(TOK_PAREN_OPEN);

        auto& list = fcn.markings.rustc_legacy_const_generics;
        do {
            auto idx_raw = lex.getTokenCheck(TOK_INTEGER).intval();
            ASSERT_BUG(lex.point_span(), idx_raw < U128(UINT_MAX), "#[rustc_legacy_const_generics(" << idx_raw << ")] too large");
            auto idx = static_cast<unsigned>(idx_raw.truncate_u64());
            ASSERT_BUG(lex.point_span(), std::find(list.begin(), list.end(), idx) == list.end(), "#[rustc_legacy_const_generics(" << idx << ")] duplicate index");
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
                auto repr_type = lex.getTokenCheck(TOK_IDENT).ident().name;
                if (repr_type == "C") {
                    switch (s->markings.repr) {
                        case AST::Struct::Markings::Repr::Rust:
                            s->markings.repr = AST::Struct::Markings::Repr::C;
                            break;
                        default:
                            // TODO: Error
                            break;
                    }
                } else if (repr_type == "packed") {
                    switch (s->markings.repr) {
                        case AST::Struct::Markings::Repr::C:
                        case AST::Struct::Markings::Repr::Rust:
                            break;
                        default:
                            // TODO: Error
                            break;
                    }
                    if (s->markings.max_field_align != 0) {
                        // TODO: Error
                    }
                    if (lex.getTokenIf(TOK_PAREN_OPEN)) {
                        auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
                        auto* val = cast<AST::ExprNodeInteger>(&*n);
                        ASSERT_BUG(n->span(), val, "#[repr(packed(...))] - alignment must be an integer");
                        auto v = val->mValue;
                        ASSERT_BUG(lex.point_span(), v > U128(0), "#[repr(packed(" << v << "))] - alignment must be non-zero");
                        ASSERT_BUG(lex.point_span(), (v & (v - 1)) == U128(0), "#[repr(packed(" << v << "))] - alignment must be a power of two");
                        ASSERT_BUG(lex.point_span(), s->markings.align_value == 0, "#[repr(packed(" << v << "))] - conflicts with previous alignment");
                        // TODO: I believe this should change the internal aligment too?
                        s->markings.max_field_align = v.truncate_u64();
                        lex.getTokenCheck(TOK_PAREN_CLOSE);
                    } else {
                        s->markings.max_field_align = 1;
                    }
                } else if (repr_type == "simd") {
                    s->markings.repr = AST::Struct::Markings::Repr::Simd;
                } else if (repr_type == "transparent") {
                    s->markings.repr = AST::Struct::Markings::Repr::Transparent;
                } else if (repr_type == "align") {
                    lex.getTokenCheck(TOK_PAREN_OPEN);
                    auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
                    auto* val = cast<AST::ExprNodeInteger>(&*n);
                    ASSERT_BUG(n->span(), val, "#[repr(align(...))] - alignment must be an integer");
                    auto v = val->mValue;
                    ASSERT_BUG(lex.point_span(), v > U128(0), "#[repr(align(" << v << "))] - alignment must be non-zero");
                    ASSERT_BUG(lex.point_span(), (v & (v - 1)) == U128(0), "#[repr(align(" << v << "))] - alignment must be a power of two");
                    s->markings.align_value = std::max(s->markings.align_value, v.truncate_u64());
                    lex.getTokenCheck(TOK_PAREN_CLOSE);
                } else if (repr_type == "no_niche") {
                    // TODO: rust-lang/rust#68303 happens with UnsafeCell and niche optionisations
                    // - Would mrustc also have this?
                } else {
                    TODO(sp, "Handle struct repr '" << repr_type << "'");
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
                auto set_repr = [&](::AST::Enum::Markings::Repr r) {
                    ASSERT_BUG(lex.point_span(), e->markings.repr == ::AST::Enum::Markings::Repr::Rust, "Multiple enum reprs set");
                    e->markings.repr = r;
                };
                auto repr_str = lex.getTokenCheck(TOK_IDENT).ident().name;
                if (repr_str == "C") {
                    // Repeated is OK
                    e->markings.is_repr_c = true;
                } else if (repr_str == "u8") {
                    set_repr(::AST::Enum::Markings::Repr::U8);
                } else if (repr_str == "u16") {
                    set_repr(::AST::Enum::Markings::Repr::U16);
                } else if (repr_str == "u32") {
                    set_repr(::AST::Enum::Markings::Repr::U32);
                } else if (repr_str == "u64") {
                    set_repr(::AST::Enum::Markings::Repr::U64);
                } else if (repr_str == "u128") {
                    set_repr(::AST::Enum::Markings::Repr::U128);
                } else if (repr_str == "usize") {
                    set_repr(::AST::Enum::Markings::Repr::Usize);
                } else if (repr_str == "i8") {
                    set_repr(::AST::Enum::Markings::Repr::I8);
                } else if (repr_str == "i16") {
                    set_repr(::AST::Enum::Markings::Repr::I16);
                } else if (repr_str == "i32") {
                    set_repr(::AST::Enum::Markings::Repr::I32);
                } else if (repr_str == "i64") {
                    set_repr(::AST::Enum::Markings::Repr::I64);
                } else if (repr_str == "i128") {
                    set_repr(::AST::Enum::Markings::Repr::I128);
                } else if (repr_str == "isize") {
                    set_repr(::AST::Enum::Markings::Repr::Isize);
                } else if (repr_str == "align") {
                    lex.getTokenCheck(TOK_PAREN_OPEN);
                    auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
                    auto* val = cast<AST::ExprNodeInteger>(&*n);
                    ASSERT_BUG(n->span(), val, "#[repr(align(...))] - alignment must be an integer");
                    auto v = val->mValue;
                    ASSERT_BUG(lex.point_span(), v > U128(0), "#[repr(align(" << v << "))] - alignment must be non-zero");
                    ASSERT_BUG(lex.point_span(), (v & (v - 1)) == U128(0), "#[repr(align(" << v << "))] - alignment must be a power of two");
                    e->markings.align_value = std::max(e->markings.align_value, v.truncate_u64());
                    lex.getTokenCheck(TOK_PAREN_CLOSE);
                } else {
                    ERROR(lex.point_span(), E0000, "Unknown enum repr '" << repr_str << "'");
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
                auto repr_str = lex.getTokenCheck(TOK_IDENT).ident().name;
                if (repr_str == "C") {
                    e->markings.repr = ::AST::Union::Markings::Repr::C;
                } else if (repr_str == "transparent") {
                    e->markings.repr = ::AST::Union::Markings::Repr::Transparent;
                } else if (repr_str == "packed") {
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
                        ASSERT_BUG(lex.point_span(), v > U128(0), "#[repr(packed(" << v << "))] - alignment must be non-zero");
                        ASSERT_BUG(lex.point_span(), (v & (v - 1)) == U128(0), "#[repr(packed(" << v << "))] - alignment must be a power of two");
                        //ASSERT_BUG(lex.point_span(), e->m_markings.align_value == 0, "#[repr(packed(" << v << "))] - conflicts with previous alignment");
                        // TODO: I believe this should change the internal aligment too?
                        //e->m_markings.max_field_align = v.truncate_u64();
                        lex.getTokenCheck(TOK_PAREN_CLOSE);
                    } else {
                        //e->m_markings.max_field_align = 1;
                    }
                } else {
                    ERROR(lex.point_span(), E0000, "Unknown union repr '" << repr_str << "'");
                }
            } while (lex.getTokenIf(TOK_COMMA));

            lex.getTokenCheck(TOK_PAREN_CLOSE);
            lex.getTokenCheck(TOK_EOF);
        } else {
            ERROR(mi.span(), E0000, "Unexpected attribute #[repr] on " << i.tag_str());
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

            s->markings.scalar_valid_start_set = true;
            s->markings.scalar_valid_start = np->mValue;
            DEBUG(path << " #[rustc_layout_scalar_valid_range_start]: " << std::hex << s->markings.scalar_valid_start);
        } else {
            TODO(sp, "#[rustc_layout_scalar_valid_range_start] on " << i.tag_str());
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
            s->markings.scalar_valid_end_set = true;
            s->markings.scalar_valid_end = np->mValue;
            DEBUG(path << " #[rustc_layout_scalar_valid_range_end]: " << std::hex << s->markings.scalar_valid_end);
        } else {
            TODO(sp, "#[rustc_layout_scalar_valid_range_end] on " << i.tag_str());
        }
    }
};

STATIC_DECORATOR("rustc_layout_scalar_valid_range_end", CHandlerRustcLayoutScalarValidRangeEnd);

class CHandlerLinkName: public ExpandDecorator {
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const AST::Attribute& mi, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        auto link_name = mi.parse_equals_string(crate, mod);
        ASSERT_BUG(sp, link_name != "", "Empty #[link_name] attribute");

        if (i.is_None()) {
        } else if (auto* fcn = i.opt_Function()) {
            ASSERT_BUG(sp, fcn->markings.link_name == "", "Duplicate #[link_name] attributes");
            fcn->markings.link_name = link_name;
        } else if (auto* st = i.opt_Static()) {
            ASSERT_BUG(sp, st->s_class() != ::AST::Static::CONST, "#[link_name] on `const`");
            ASSERT_BUG(sp, st->markings.link_name == "", "Duplicate #[link_name] attributes");
            st->markings.link_name = link_name;
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
        auto link_section = mi.parse_equals_string(crate, mod);
        ASSERT_BUG(sp, link_section != "", "Empty #[link_section] attribute");

        if (i.is_None()) {
        } else if (auto* fcn = i.opt_Function()) {
            ASSERT_BUG(sp, fcn->markings.link_section == "", "Duplicate #[link_section] attributes");
            fcn->markings.link_section = link_section;
        } else if (auto* st = i.opt_Static()) {
            ASSERT_BUG(sp, st->s_class() != ::AST::Static::CONST, "#[link_section] on `const`");
            ASSERT_BUG(sp, st->markings.link_section == "", "Duplicate #[link_section] attributes");
            st->markings.link_section = link_section;
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
            std::string lib_name;
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
                    link.lib_name = v;
                } else if (key == "kind") {
                    lex.getTokenCheck(TOK_EQUAL);
                    auto v = lex.getTokenCheck(TOK_STRING).str();
                    if (v == "") {
                        ERROR(sp, E0000, "Empty `kind` on extern block #[link]");
                    }
                    // TODO: save and use the kind
                } else if (key == "cfg") {
                    emit &= check_cfg_stream(lex);
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
            if (link.lib_name == "") {
                ERROR(sp, E0000, "No name in `#[link]`");
            }
            if (emit) {
                b->libraries.push_back(std::move(link));
            }
            lex.getTokenCheck(TOK_PAREN_CLOSE);
            lex.getTokenCheck(TOK_EOF);
        } else {
            TODO(sp, "#[link] on " << i.tag_str());
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
        auto linkage_str = tok.str();
        lex.getTokenCheck(TOK_EOF);

        auto linkage = AST::Linkage::Default;
        if (linkage_str == "extern_weak") {
            linkage = AST::Linkage::ExternWeak;
        } else if (linkage_str == "weak") {
            linkage = AST::Linkage::Weak;
        } else if (linkage_str == "external") {
            //linkage = AST::Linkage::External;
        } else {
            TODO(sp, "#[linkage=\"" << linkage_str << "\"]");
        }

        if (auto* f = i.opt_Function()) {
            switch (linkage) {
                case AST::Linkage::Default:
                case AST::Linkage::Weak:
                    break;
                default:
                    TODO(sp, "#[linkage=\"" << linkage_str << "\"] on " << i.tag_str());
            }
            f->markings.linkage = linkage;
        } else if (auto* f = i.opt_Static()) {
            switch (linkage) {
                case AST::Linkage::Default:
                case AST::Linkage::Weak:
                case AST::Linkage::ExternWeak:
                    break;
                default:
                    TODO(sp, "#[linkage=\"" << linkage_str << "\"] on " << i.tag_str());
            }
            f->markings.linkage = linkage;
        } else {
            TODO(sp, "#[linkage] - " << i.tag_str() << " " << path << ": " << mi);
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
                e->set_abi("rust-intrinsic");
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
                    e->markings.link_name = path.nodes.back().c_str();
                } else if (auto* e = i.opt_Static()) {
                    e->markings.link_name = path.nodes.back().c_str();
                } else {
                    ERROR(sp, E0000, "#[unsafe(" << ident << ")] on bad item: " << i.tag_str());
                }
            } else if (ident == "link_section") {
                lex.getTokenCheck(TOK_EQUAL);
                auto s = lex.getTokenCheck(TOK_STRING).str();

                DEBUG("#[unsafe(link_section)] " << path << " in `" << s);
                if (auto* e = i.opt_Function()) {
                    e->markings.link_section = s;
                } else if (auto* e = i.opt_Static()) {
                    e->markings.link_section = s;
                } else {
                    ERROR(sp, E0000, "#[unsafe(" << ident << ")] on bad item: " << i.tag_str());
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
                    e->markings.is_naked = true;
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
        auto name = mi.parse_equals_string(crate, crate.rootModule);
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
        auto name = mi.parse_equals_string(crate, crate.rootModule);
        crate.set_crate_name(name);
    }
};

class DecoratorFeature: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Pre;
    }

    void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate) const override {
        mi.parse_paren_ident_list([&](const Span&, RcString feature) {
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
            ERROR(sp, E0000, "#[allocator] can only be put on functions and the crate - found on " << i.tag_str());
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
    const RcString rcstringSelf = RcString::new_interned("Self");
    const RcString rcstringH = RcString::new_interned("H");
    const RcString rcstring_self = RcString::new_interned("self");
    const RcString rcstring_v = RcString::new_interned("v");
    const RcString rcstring_s = RcString::new_interned("s");
    const RcString rcstring_fmt = RcString::new_interned("fmt");

    const RcString rcstring_res = RcString::new_interned("res");

    const RcString rcstring_f = RcString::new_interned("f");
    const RcString rcstring_field = RcString::new_interned("field");

    const RcString rcstring_write_str = RcString::new_interned("write_str");
    const RcString rcstring_finish = RcString::new_interned("finish");

    const RcString rcstringClone = RcString::new_interned("Clone");
    const RcString rcstring_clone = RcString::new_interned("clone");

    const RcString rcstring_state = RcString::new_interned("state");

    const RcString rcstring_assert_receiver_is_total_eq = RcString::new_interned("assert_receiver_is_total_eq");

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

static AST::Path get_path(const RcString& core_name, const char* c1, const char* c2) {
    return AST::AbsolutePath(core_name, {RcString::new_interned(c1), RcString::new_interned(c2)});
}

static AST::Path get_path(const RcString& core_name, const char* c1, const char* c2, const char* c3) {
    return AST::AbsolutePath(core_name, {RcString::new_interned(c1), RcString::new_interned(c2), RcString::new_interned(c3)});
}

static std::unique_ptr<AST::ExprNodeBlock> new_block(const Span& sp) {
    auto rv = ::std::make_unique<AST::ExprNodeBlock>();
    rv->set_span(sp);
    return rv;
}

static inline AST::ExprNodeP mk_exprnodep(AST::ExprNode* en) {
    return AST::ExprNodeP(en);
}

//#define NEWNODE(type, ...)  mk_exprnodep(new type(__VA_ARGS__))
#define NEWNODE(type, ...) mk_exprnodep(new AST::ExprNode##type(__VA_ARGS__))

static void make_refpat_a(const Span& sp, AST::ExprNodeBlock& block, ::std::vector<AST::Pattern>& pats_a, const ::std::vector<AST::TupleItem>& sub_types, ::std::function<AST::ExprNodeP(size_t, AST::ExprNodeP)> cb) {
    ::std::vector<AST::ExprNodeBlock::Line> nodes;
    for (size_t idx = 0; idx < sub_types.size(); idx++) {
        auto name_a = RcString::new_interned(FMT("a" << idx));
        pats_a.push_back(::AST::Pattern(::AST::Pattern::TagBind(), sp, name_a, ::AST::PatternBinding::Type::REF));
        block.push_stmt(cb(idx, NEWNODE(NamedValue, AST::Path(name_a))));
    }
}

static void make_refpat_a(const Span& sp, AST::ExprNodeBlock& block, ::std::vector<AST::StructPatternEntry>& pats_a, const ::std::vector<AST::StructItem>& fields, ::std::function<AST::ExprNodeP(size_t, AST::ExprNodeP)> cb) {
    ::std::vector<AST::ExprNodeBlock::Line> nodes;
    size_t idx = 0;
    for (const auto& fld : fields) {
        auto name_a = RcString::new_interned(FMT("a" << fld.mName));
        pats_a.push_back(AST::StructPatternEntry{AST::AttributeList(), fld.mName, ::AST::Pattern(::AST::Pattern::TagBind(), sp, name_a, ::AST::PatternBinding::Type::REF)});
        block.push_stmt(cb(idx, NEWNODE(NamedValue, AST::Path(name_a))));
        idx++;
    }
}

static void make_refpat_ab(const Span& sp, AST::ExprNodeBlock& block, ::std::vector<AST::Pattern>& pats_a, ::std::vector<AST::Pattern>& pats_b, const ::std::vector<AST::TupleItem>& sub_types, ::std::function<AST::ExprNodeP(size_t, AST::ExprNodeP, AST::ExprNodeP)> cb) {
    for (size_t idx = 0; idx < sub_types.size(); idx++) {
        auto name_a = RcString::new_interned(FMT("a" << idx));
        auto name_b = RcString::new_interned(FMT("b" << idx));
        pats_a.push_back(::AST::Pattern(::AST::Pattern::TagBind(), sp, name_a, ::AST::PatternBinding::Type::REF));
        pats_b.push_back(::AST::Pattern(::AST::Pattern::TagBind(), sp, name_b, ::AST::PatternBinding::Type::REF));
        block.push_stmt(cb(idx, NEWNODE(NamedValue, AST::Path(name_a)), NEWNODE(NamedValue, AST::Path(name_b))));
    }
}

static void make_refpat_ab(const Span& sp, AST::ExprNodeBlock& block, ::std::vector<AST::StructPatternEntry>& pats_a, ::std::vector<AST::StructPatternEntry>& pats_b, const ::std::vector<AST::StructItem>& fields, ::std::function<AST::ExprNodeP(size_t, AST::ExprNodeP, AST::ExprNodeP)> cb) {
    size_t idx = 0;
    for (const auto& fld : fields) {
        auto name_a = RcString::new_interned(FMT("a" << fld.mName));
        auto name_b = RcString::new_interned(FMT("b" << fld.mName));
        pats_a.push_back(AST::StructPatternEntry{AST::AttributeList(), fld.mName, ::AST::Pattern(::AST::Pattern::TagBind(), sp, name_a, ::AST::PatternBinding::Type::REF)});
        pats_b.push_back(AST::StructPatternEntry{AST::AttributeList(), fld.mName, ::AST::Pattern(::AST::Pattern::TagBind(), sp, name_b, ::AST::PatternBinding::Type::REF)});
        block.push_stmt(cb(idx, NEWNODE(NamedValue, AST::Path(name_a)), NEWNODE(NamedValue, AST::Path(name_b))));
        idx++;
    }
}

struct DeriveOpts {
    RcString core_name;
};

/// Interface for derive handlers
struct Deriver {
    virtual ~Deriver() = default;
    virtual const char* trait_name() const = 0;
    virtual AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Struct& str) const = 0;
    virtual AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Enum& enm) const = 0;

    virtual AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Union& unn) const {
        ERROR(sp, E0000, "Cannot derive(" << trait_name() << ") on union");
    }

    void iterate_struct_fields(const AST::Struct& str, ::std::function<void(RcString)> cb) const {
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
                    auto fld_name = RcString::new_interned(FMT(idx));
                    cb(fld_name);
                }
            }
        }
    }

    AST::GenericParams get_params_with_bounds(const Span& sp, const AST::GenericParams& p, const AST::Path& trait_path, ::std::vector<TypeRef> additional_bounded_types) const {
        AST::GenericParams params = p.clone();

        // TODO: Get bounds based on generic (or similar) types used within the type.
        // - How would this code (that runs before resolve) know what's a generic and what's a local type?
        // - Searches within the type for a Path that starts with that param.

        unsigned int i = 0;
        for (const auto& arg : params.mParams) {
            if (const auto* e = arg.opt_Type()) {
                params.add_bound(::AST::GenericBound::make_IsTrait({sp, {}, TypeRef(sp, e->name(), i), {}, trait_path}));
                i++;
            }
        }

        // For each field type
        // - Locate used generic parameters in the type (and sub-types that directly use said parameter)
        for (auto& ty : additional_bounded_types) {
            params.add_bound(::AST::GenericBound::make_IsTrait({sp, {}, mv$(ty), {}, trait_path}));
        }

        return params;
    }

    ::std::vector<TypeRef> get_field_bounds(const AST::Struct& str) const {
        ::std::vector<TypeRef> ret;
        TU_MATCH(AST::StructData, (str.mData), (e), (Unit, ), (Struct, for (const auto& fld : e.ents) { add_field_bound_from_ty(str.params(), ret, fld.mType); }), (Tuple, for (const auto& ent : e.ents) { add_field_bound_from_ty(str.params(), ret, ent.mType); }))
        return ret;
    }

    ::std::vector<TypeRef> get_field_bounds(const AST::Enum& enm) const {
        ::std::vector<TypeRef> ret;

        for (const auto& v : enm.variants()) {
            TU_MATCH(::AST::EnumVariantData, (v.mData), (e), (Unit, ), (Tuple, for (const auto& ent : e.mItems) { add_field_bound_from_ty(enm.params(), ret, ent.mType); }), (Struct, for (const auto& fld : e.fields) { add_field_bound_from_ty(enm.params(), ret, fld.mType); }))
        }

        return ret;
    }

    ::std::vector<TypeRef> get_field_bounds(const AST::Union& unn) const {
        ::std::vector<TypeRef> ret;
        for (const auto& fld : unn.mVariants) {
            add_field_bound_from_ty(unn.params(), ret, fld.mType);
        }
        return ret;
    }

    void add_field_bound_from_ty(const AST::GenericParams& params, ::std::vector<TypeRef>& out_list, const TypeRef& ty) const {
        struct H {
            static void visit_nodes(const Deriver& self, const AST::GenericParams& params, ::std::vector<TypeRef>& out_list, const ::std::vector<AST::PathNode>& nodes) {
                for (const auto& node : nodes) {
                    for (const auto& e : node.args().entries) {
                        TU_MATCH_HDRA( (e), {)
                        default:
                            break;
                            TU_ARMA(Type, ty) {
                                self.add_field_bound_from_ty(params, out_list, ty);
                            }
                            TU_ARMA(AssociatedTyEqual, aty) {
                                self.add_field_bound_from_ty(params, out_list, aty.second);
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
                for (const auto& sty : e.inner_types) {
                    add_field_bound_from_ty(params, out_list, sty);
                }
            }
            TU_ARMA(Borrow, e) {
                add_field_bound_from_ty(params, out_list, *e.inner);
            }
            TU_ARMA(Pointer, e) {
                add_field_bound_from_ty(params, out_list, *e.inner);
            }
            TU_ARMA(Array, e) {
                add_field_bound_from_ty(params, out_list, *e.inner);
            }
            TU_ARMA(Slice, e) {
                add_field_bound_from_ty(params, out_list, *e.inner);
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
                                    add_field_bound(out_list, ty);
                                    break;
                                }
                            }
                        }
                        H::visit_nodes(*this, params, out_list, pe.nodes);
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

    void add_field_bound(::std::vector<TypeRef>& out_list, const TypeRef& type) const {
        for (const auto& ty : out_list) {
            if (ty == type) {
                return;
            }
        }

        out_list.push_back(type.clone());
    }
};

/// 'Debug' derive handler
class DeriverDebug: public Deriver {
    AST::Impl make_ret(Span sp, const RcString& core_name, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> types_to_bound, AST::ExprNodeP node) const {
        const AST::Path debug_trait = get_path(core_name, "fmt", "Debug");

        AST::Function fcn(sp, TypeRef(sp, get_path(core_name, "fmt", "Result")), vec$(AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstring_self), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), false, mktypeSelf(sp))), AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstring_f), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), true, TypeRef(sp, get_path(core_name, "fmt", "Formatter"))))));
        fcn.set_code(NEWNODE(Block, mv$(node)));

        AST::GenericParams params = get_params_with_bounds(sp, p, debug_trait, mv$(types_to_bound));

        AST::Impl rv(AST::ImplDef(mv$(params), make_spanned(sp, debug_trait), type.clone()));
        rv.add_function(sp, {}, AST::Visibility::make_bare_private(), false, rcstring_fmt, mv$(fcn));
        return mv$(rv);
    }

public:
    const char* trait_name() const override {
        return "Debug";
    }

    AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Struct& str) const override {
        ::std::string name = type.path().nodes().back().name().c_str();

        // Generate code for Debug
        AST::ExprNodeP node;
        TU_MATCH_HDRA((str.mData), {)
        TU_ARMA(Unit, e) {
                node = NEWNODE(NamedValue, AST::Path(rcstring_f));
                node = NEWNODE(CallMethod, mv$(node), AST::PathNode(rcstring_write_str, {}), vec$(NEWNODE(String, name)));
            }
            TU_ARMA(Struct, e) {
                node = NEWNODE(NamedValue, AST::Path(rcstring_f));
                std::vector<AST::ExprNodeBlock::Line> nodes;
                nodes.push_back({true, NEWNODE(LetBinding, AST::Pattern(AST::Pattern::TagBind(), sp, rcstring_s), TypeRef(sp), NEWNODE(CallMethod, mv$(node), AST::PathNode(RcString::new_interned("debug_struct"), {}), vec$(NEWNODE(String, name))))});
                for (const auto& fld : e.ents) {
                    nodes.push_back({true, NEWNODE(CallMethod, NEWNODE(NamedValue, AST::Path(rcstring_s)), AST::PathNode(rcstring_field, {}), vec$(NEWNODE(String, fld.mName.c_str()), NEWNODE(UniOp, AST::ExprNodeUniOp::REF, NEWNODE(UniOp, AST::ExprNodeUniOp::REF, NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstring_self)), fld.mName)))))});
                }
                nodes.push_back({false, NEWNODE(CallMethod, NEWNODE(NamedValue, AST::Path(rcstring_s)), AST::PathNode(rcstring_finish, {}), {})});
                node = NEWNODE(Block, mv$(nodes));
            }
            TU_ARMA(Tuple, e) {
                node = NEWNODE(NamedValue, AST::Path(rcstring_f));
                node = NEWNODE(CallMethod, mv$(node), AST::PathNode(RcString::new_interned("debug_tuple"), {}), vec$(NEWNODE(String, name)));
                for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                    node = NEWNODE(CallMethod, mv$(node), AST::PathNode(rcstring_field, {}), vec$(NEWNODE(UniOp, AST::ExprNodeUniOp::REF, NEWNODE(UniOp, AST::ExprNodeUniOp::REF, NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstring_self)), RcString::new_interned(FMT(idx)))))));
                }
                node = NEWNODE(CallMethod, mv$(node), AST::PathNode(rcstring_finish, {}), {});
            }
        }

        return this->make_ret(sp, opts.core_name, p, type, this->get_field_bounds(str), mv$(node));
    }

    AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Enum& enm) const override {
        AST::Path base_path = *type.mData.as_Path();
        base_path.nodes().back() = base_path.nodes().back().name();

        ::std::vector<AST::ExprNodeMatchArm> arms;
        for (const auto& v : enm.variants()) {
            AST::ExprNodeP code;
            AST::Pattern pat_a;

            AST::Path variant_path = base_path + v.mName;

            TU_MATCH_HDRA( (v.mData), {)
            TU_ARMA(Unit, e) {
                    code = NEWNODE(CallMethod, NEWNODE(NamedValue, AST::Path(rcstring_f)), AST::PathNode(rcstring_write_str, {}), vec$(NEWNODE(String, v.mName.c_str())));
                    pat_a = AST::Pattern(AST::Pattern::TagValue(), sp, AST::Pattern::Value::make_Named(variant_path));
                }
                TU_ARMA(Tuple, e) {
                    ::std::vector<AST::Pattern> pats_a;
                    auto block = new_block(sp);
                    block->push_stmt(NEWNODE(LetBinding, AST::Pattern(AST::Pattern::TagBind(), sp, rcstring_s), TypeRef(sp), NEWNODE(CallMethod, NEWNODE(NamedValue, AST::Path(rcstring_f)), AST::PathNode(RcString::new_interned("debug_tuple"), {}), vec$(NEWNODE(String, v.mName.c_str())))));

                    auto s_ent = NEWNODE(NamedValue, AST::Path(rcstring_s));
                    make_refpat_a(sp, *block, pats_a, e.mItems, [&](size_t idx, auto a) {
                        return NEWNODE(CallMethod, s_ent->clone(), AST::PathNode(rcstring_field, {}), vec$(mv$(a)));
                    });
                    block->push_tail_expr(NEWNODE(CallMethod, mv$(s_ent), AST::PathNode(rcstring_finish, {}), {}));
                    code = mk_exprnodep(block.release());
                    pat_a = AST::Pattern(AST::Pattern::TagNamedTuple(), sp, variant_path, mv$(pats_a));
                }
                TU_ARMA(Struct, e) {
                    ::std::vector<AST::StructPatternEntry> pats_a;
                    auto block = new_block(sp);
                    block->push_stmt(NEWNODE(LetBinding, AST::Pattern(AST::Pattern::TagBind(), sp, rcstring_s), TypeRef(sp), NEWNODE(CallMethod, NEWNODE(NamedValue, AST::Path(rcstring_f)), AST::PathNode(RcString::new_interned("debug_struct"), {}), vec$(NEWNODE(String, v.mName.c_str())))));

                    auto s_ent = NEWNODE(NamedValue, AST::Path(rcstring_s));
                    make_refpat_a(sp, *block, pats_a, e.fields, [&](size_t idx, auto a) {
                        return NEWNODE(CallMethod, s_ent->clone(), AST::PathNode(rcstring_field, {}), vec$(NEWNODE(String, e.fields[idx].mName.c_str()), mv$(a)));
                    });
                    block->push_tail_expr(NEWNODE(CallMethod, mv$(s_ent), AST::PathNode(rcstring_finish, {}), {}));

                    code = mk_exprnodep(block.release());
                    pat_a = AST::Pattern(AST::Pattern::TagStruct(), sp, variant_path, mv$(pats_a), true);
                }
            }

            ::std::vector< AST::Pattern>    pats;
            pats.push_back( AST::Pattern(AST::Pattern::TagReference(), sp, false, mv$(pat_a)) );

            arms.push_back(AST::ExprNodeMatchArm(
                mv$(pats),
                {},
                mv$(code)
                ));
        }
        AST::ExprNodeP node = NEWNODE(Match, NEWNODE(NamedValue, AST::Path(rcstring_self)), mv$(arms));

        return this->make_ret(sp, opts.core_name, p, type, this->get_field_bounds(enm), mv$(node));
    }
} g_derive_debug;

// ---- Comparisons

class DeriverInnerCompare: public Deriver {
protected:
    /// Create a final output impl block
    virtual AST::Impl make_ret(Span sp, const RcString& core_name, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> types_to_bound, AST::ExprNodeP node) const = 0;
    /// Compare two values, early returning if no more comparisons should happen
    virtual AST::ExprNodeP compare_and_ret(Span sp, const RcString& core_name, AST::ExprNodeP v1, AST::ExprNodeP v2) const = 0;
    /// Get the return value for if `compare_and_ret` didn't return early
    virtual AST::ExprNodeP equal_value(Span sp, const RcString& core_name) const = 0;
    /// Get the return value for a mismatch in enum variants
    virtual AST::ExprNodeP enum_mismatch(Span sp, const RcString& core_name) const = 0;

public:
    // Struct
    AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Struct& str) const override {
        auto block = new_block(sp);

        this->iterate_struct_fields(str, [&](RcString fld_name) {
            block->push_stmt(this->compare_and_ret(sp, opts.core_name, NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstring_self)), fld_name), NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstring_v)), fld_name)));
        });
        block->push_tail_expr(this->equal_value(sp, opts.core_name));

        return this->make_ret(sp, opts.core_name, p, type, this->get_field_bounds(str), mk_exprnodep(block.release()));
    }

    // Enum
    AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Enum& enm) const override {
        AST::Path base_path = *type.mData.as_Path();
        base_path.nodes().back().args() = ::AST::PathParams();
        ::std::vector<AST::ExprNodeMatchArm> arms;

        for (const auto& v : enm.variants()) {
            AST::ExprNodeP code;
            AST::Pattern pat_a;
            AST::Pattern pat_b;
            auto variant_path = base_path + v.mName;

            TU_MATCH_HDRA( (v.mData), {)
            TU_ARMA(Unit, e) {
                    code = this->equal_value(sp, opts.core_name);
                    pat_a = AST::Pattern(AST::Pattern::TagValue(), sp, AST::Pattern::Value::make_Named(variant_path));
                    pat_b = AST::Pattern(AST::Pattern::TagValue(), sp, AST::Pattern::Value::make_Named(variant_path));
                }
                TU_ARMA(Tuple, e) {
                    auto block = new_block(sp);
                    ::std::vector<AST::Pattern> pats_a;
                    ::std::vector<AST::Pattern> pats_b;

                    make_refpat_ab(sp, *block, pats_a, pats_b, e.mItems, [&](auto idx, auto a, auto b) {
                        return this->compare_and_ret(sp, opts.core_name, mv$(a), mv$(b));
                    });
                    block->push_tail_expr(this->equal_value(sp, opts.core_name));

                    pat_a = AST::Pattern(AST::Pattern::TagNamedTuple(), sp, variant_path, mv$(pats_a));
                    pat_b = AST::Pattern(AST::Pattern::TagNamedTuple(), sp, variant_path, mv$(pats_b));
                    code = mk_exprnodep(block.release());
                }
                TU_ARMA(Struct, e) {
                    auto block = new_block(sp);
                    ::std::vector<AST::StructPatternEntry> pats_a;
                    ::std::vector<AST::StructPatternEntry> pats_b;

                    make_refpat_ab(sp, *block, pats_a, pats_b, e.fields, [&](const auto& name, auto a, auto b) {
                        return this->compare_and_ret(sp, opts.core_name, mv$(a), mv$(b));
                    });
                    block->push_tail_expr(this->equal_value(sp, opts.core_name));

                    pat_a = AST::Pattern(AST::Pattern::TagStruct(), sp, variant_path, mv$(pats_a), true);
                    pat_b = AST::Pattern(AST::Pattern::TagStruct(), sp, variant_path, mv$(pats_b), true);
                    code = mk_exprnodep(block.release());
                }
            }

            ::std::vector< AST::Pattern>    pats;
            {
                ::std::vector<AST::Pattern> tuple_pats;
                tuple_pats.push_back(AST::Pattern(AST::Pattern::TagReference(), sp, false, mv$(pat_a)));
                tuple_pats.push_back(AST::Pattern(AST::Pattern::TagReference(), sp, false, mv$(pat_b)));
                pats.push_back(AST::Pattern(AST::Pattern::TagTuple(), sp, mv$(tuple_pats)));
            }

            arms.push_back(AST::ExprNodeMatchArm(
                mv$(pats),
                {},
                mv$(code)
                ));
        }

        // Default arm
        {
            arms.push_back(AST::ExprNodeMatchArm(::make_vec1(AST::Pattern()), {}, this->enum_mismatch(sp, opts.core_name)));
        }

        ::std::vector<AST::ExprNodeP> vals;
        vals.push_back(NEWNODE(NamedValue, AST::Path(rcstring_self)));
        vals.push_back(NEWNODE(NamedValue, AST::Path(rcstring_v)));
        return this->make_ret(sp, opts.core_name, p, type, this->get_field_bounds(enm), NEWNODE(Match, NEWNODE(Tuple, mv$(vals)), mv$(arms)));
    }
};

class DeriverPartialEq: public DeriverInnerCompare {
    AST::Impl make_ret(Span sp, const RcString& core_name, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> types_to_bound, AST::ExprNodeP node) const override {
        const AST::Path trait_path = get_path(core_name, "cmp", "PartialEq");

        AST::Function fcn(sp, TypeRef(sp, CORETYPE_BOOL), vec$(AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstring_self), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), false, mktypeSelf(sp))), AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstring_v), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), false, mktypeSelf(sp)))));
        fcn.set_code(NEWNODE(Block, mv$(node)));

        AST::GenericParams params = get_params_with_bounds(sp, p, trait_path, mv$(types_to_bound));

        AST::Impl rv(AST::ImplDef(mv$(params), make_spanned(sp, trait_path), type.clone()));
        rv.add_function(sp, {}, AST::Visibility::make_bare_private(), false, RcString::new_interned("eq"), mv$(fcn));
        return mv$(rv);
    }

    AST::ExprNodeP compare_and_ret(Span sp, const RcString& core_name, AST::ExprNodeP v1, AST::ExprNodeP v2) const override {
        std::vector<AST::ExprNodeIf::Arm> arms;
        arms.push_back(AST::ExprNodeIf::Arm{make_vec1(AST::IfLetCondition{{}, NEWNODE(BinOp, AST::ExprNodeBinOp::CMPNEQU, mv$(v1), mv$(v2))}), NEWNODE(Flow, AST::ExprNodeFlow::RETURN, "", NEWNODE(Bool, false))});
        return NEWNODE(If, std::move(arms), nullptr);
    }

    AST::ExprNodeP equal_value(Span sp, const RcString& core_name) const override {
        return NEWNODE(Bool, true);
    }

    AST::ExprNodeP enum_mismatch(Span sp, const RcString& core_name) const override {
        return NEWNODE(Bool, false);
    }

public:
    const char* trait_name() const override {
        return "PartialEq";
    }
} g_derive_partialeq;

class DeriverPartialOrd: public DeriverInnerCompare {
    AST::Impl make_ret(Span sp, const RcString& core_name, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> types_to_bound, AST::ExprNodeP node) const override {
        const AST::Path trait_path = get_path(core_name, "cmp", "PartialOrd");
        const AST::Path path_ordering = get_path(core_name, "cmp", "Ordering");

        AST::Path path_option_ordering = get_path(core_name, "option", "Option");
        path_option_ordering.nodes().back().args().entries.push_back(TypeRef(sp, path_ordering));

        AST::Function fcn(sp, TypeRef(sp, path_option_ordering), vec$(AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstring_self), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), false, mktypeSelf(sp))), AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstring_v), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), false, mktypeSelf(sp)))));
        fcn.set_code(NEWNODE(Block, mv$(node)));

        AST::GenericParams params = get_params_with_bounds(sp, p, trait_path, mv$(types_to_bound));

        AST::Impl rv(AST::ImplDef(mv$(params), make_spanned(sp, trait_path), type.clone()));
        rv.add_function(sp, {}, AST::Visibility::make_bare_private(), false, RcString::new_interned("partial_cmp"), mv$(fcn));
        return mv$(rv);
    }

    AST::ExprNodeP compare_and_ret(Span sp, const RcString& core_name, AST::ExprNodeP v1, AST::ExprNodeP v2) const override {
        return NEWNODE(Match, NEWNODE(CallPath, get_path(core_name, "cmp", "PartialOrd", "partial_cmp"), ::make_vec2(NEWNODE(UniOp, AST::ExprNodeUniOp::REF, mv$(v1)), NEWNODE(UniOp, AST::ExprNodeUniOp::REF, mv$(v2)))), ::make_vec3(::AST::ExprNodeMatchArm(::make_vec1(AST::Pattern(AST::Pattern::TagValue(), sp, get_path(core_name, "option", "Option", "None"))), {}, NEWNODE(Flow, AST::ExprNodeFlow::RETURN, "", NEWNODE(NamedValue, get_path(core_name, "option", "Option", "None")))), ::AST::ExprNodeMatchArm(::make_vec1(AST::Pattern(AST::Pattern::TagNamedTuple(), sp, get_path(core_name, "option", "Option", "Some"), ::make_vec1(AST::Pattern(AST::Pattern::TagValue(), sp, get_path(core_name, "cmp", "Ordering", "Equal"))))), {}, NEWNODE(Tuple, ::std::vector<AST::ExprNodeP>())), ::AST::ExprNodeMatchArm(::make_vec1(AST::Pattern(AST::Pattern::TagBind(), sp, rcstring_res)), {}, NEWNODE(Flow, AST::ExprNodeFlow::RETURN, "", NEWNODE(NamedValue, AST::Path(rcstring_res))))));
    }

    AST::ExprNodeP equal_value(Span sp, const RcString& core_name) const override {
        return NEWNODE(CallPath, get_path(core_name, "option", "Option", "Some"), ::make_vec1(NEWNODE(NamedValue, get_path(core_name, "cmp", "Ordering", "Equal"))));
    }

    AST::ExprNodeP enum_mismatch(Span sp, const RcString& core_name) const override {
        return NEWNODE(CallPath, get_path(core_name, "cmp", "PartialOrd", "partial_cmp"), ::make_vec2(NEWNODE(UniOp, AST::ExprNodeUniOp::REF, NEWNODE(CallPath, get_path(core_name, "intrinsics", "discriminant_value"), make_vec1(NEWNODE(NamedValue, AST::Path(rcstring_self))))), NEWNODE(UniOp, AST::ExprNodeUniOp::REF, NEWNODE(CallPath, get_path(core_name, "intrinsics", "discriminant_value"), make_vec1(NEWNODE(NamedValue, AST::Path(rcstring_v)))))));
    }

public:
    const char* trait_name() const override {
        return "PartialOrd";
    }
} g_derive_partialord;

class DeriverEq: public Deriver {
    AST::Path get_trait_path(const RcString& core_name) const {
        return get_path(core_name, "cmp", "Eq");
    }

    AST::Impl make_ret(Span sp, const RcString& core_name, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> types_to_bound, AST::ExprNodeP node) const {
        const AST::Path trait_path = this->get_trait_path(core_name);

        AST::Function fcn(sp, TypeRef(TypeRef::TagUnit(), sp), vec$(AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstring_self), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), false, mktypeSelf(sp)))));
        fcn.set_code(NEWNODE(Block, mv$(node)));

        AST::GenericParams params = get_params_with_bounds(sp, p, trait_path, mv$(types_to_bound));

        AST::Impl rv(AST::ImplDef(mv$(params), make_spanned(sp, trait_path), type.clone()));
        rv.add_function(sp, {}, AST::Visibility::make_bare_private(), false, rcstring_assert_receiver_is_total_eq, mv$(fcn));
        return mv$(rv);
    }

    AST::ExprNodeP assert_is_eq(const AST::Path& method_path, AST::ExprNodeP val) const {
        return NEWNODE(CallPath, AST::Path(method_path), vec$(NEWNODE(UniOp, AST::ExprNodeUniOp::REF, mv$(val))));
    }

    AST::ExprNodeP field(const ::std::string& name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstring_self)), RcString::new_interned(name));
    }

    AST::ExprNodeP field(const RcString& name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstring_self)), name);
    }

public:
    const char* trait_name() const override {
        return "Eq";
    }

    AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Struct& str) const override {
        const AST::Path assert_method_path = this->get_trait_path(opts.core_name) + rcstring_assert_receiver_is_total_eq;

        auto block = new_block(sp);
        this->iterate_struct_fields(str, [&](RcString name) {
            block->push_stmt(this->assert_is_eq(assert_method_path, this->field(name)));
        });

        return this->make_ret(sp, opts.core_name, p, type, this->get_field_bounds(str), mk_exprnodep(block.release()));
    }

    AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Enum& enm) const override {
        const AST::Path assert_method_path = this->get_trait_path(opts.core_name) + rcstring_assert_receiver_is_total_eq;

        AST::Path base_path = *type.mData.as_Path();
        base_path.nodes().back().args() = ::AST::PathParams();
        ::std::vector<AST::ExprNodeMatchArm> arms;

        for (const auto& v : enm.variants()) {
            AST::ExprNodeP code;
            AST::Pattern pat_a;
            auto variant_path = base_path + v.mName;

            TU_MATCH_HDRA( (v.mData), {)
            TU_ARMA(Unit, e) {
                    code = NEWNODE(Block);
                    pat_a = AST::Pattern(AST::Pattern::TagValue(), sp, AST::Pattern::Value::make_Named(variant_path));
                }
                TU_ARMA(Tuple, e) {
                    auto block = new_block(sp);
                    ::std::vector<AST::Pattern> pats_a;
                    make_refpat_a(sp, *block, pats_a, e.mItems, [&](size_t idx, auto a) {
                        return this->assert_is_eq(assert_method_path, mv$(a));
                    });

                    pat_a = AST::Pattern(AST::Pattern::TagNamedTuple(), sp, variant_path, mv$(pats_a));
                    code = mk_exprnodep(block.release());
                }
                TU_ARMA(Struct, e) {
                    auto block = new_block(sp);
                    ::std::vector<AST::StructPatternEntry> pats_a;
                    make_refpat_a(sp, *block, pats_a, e.fields, [&](size_t idx, auto a) {
                        return this->assert_is_eq(assert_method_path, mv$(a));
                    });

                    pat_a = AST::Pattern(AST::Pattern::TagStruct(), sp, variant_path, mv$(pats_a), true);
                    code = mk_exprnodep(block.release());
                }
            }

            ::std::vector< AST::Pattern>    pats;
            pats.push_back( AST::Pattern(AST::Pattern::TagReference(), sp, false, mv$(pat_a)) );

            arms.push_back(AST::ExprNodeMatchArm(
                mv$(pats),
                {},
                mv$(code)
                ));
        }

        return this->make_ret(sp, opts.core_name, p, type, this->get_field_bounds(enm), NEWNODE(Match, NEWNODE(NamedValue, AST::Path(rcstring_self)), mv$(arms)));
    }

    AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Union& unn) const override {
        // Eq is just a marker, so it's valid to derive for union
        const AST::Path assert_method_path = this->get_trait_path(opts.core_name) + rcstring_assert_receiver_is_total_eq;
        auto block = new_block(sp);

        for (const auto& fld : unn.mVariants) {
            block->push_stmt(this->assert_is_eq(assert_method_path, this->field(fld.mName)));
        }

        return this->make_ret(sp, opts.core_name, p, type, this->get_field_bounds(unn), mk_exprnodep(block.release()));
    }
} g_derive_eq;

class DeriverOrd: public DeriverInnerCompare {
    AST::Impl make_ret(Span sp, const RcString& core_name, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> types_to_bound, AST::ExprNodeP node) const override {
        const AST::Path trait_path = get_path(core_name, "cmp", "Ord");
        const AST::Path path_ordering = get_path(core_name, "cmp", "Ordering");

        AST::Function fcn(sp, TypeRef(sp, path_ordering), vec$(AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstring_self), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), false, mktypeSelf(sp))), AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstring_v), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), false, mktypeSelf(sp)))));
        fcn.set_code(NEWNODE(Block, mv$(node)));

        AST::GenericParams params = get_params_with_bounds(sp, p, trait_path, mv$(types_to_bound));

        AST::Impl rv(AST::ImplDef(mv$(params), make_spanned(sp, trait_path), type.clone()));
        rv.add_function(sp, {}, AST::Visibility::make_bare_private(), false, RcString::new_interned("cmp"), mv$(fcn));
        return mv$(rv);
    }

    AST::ExprNodeP compare_and_ret(Span sp, const RcString& core_name, AST::ExprNodeP v1, AST::ExprNodeP v2) const override {
        return NEWNODE(
            Match,
            NEWNODE(
                CallPath,
                get_path(core_name, "cmp", "Ord", "cmp"),
                // TODO: Optional Ref?
                ::make_vec2(NEWNODE(UniOp, AST::ExprNodeUniOp::REF, mv$(v1)), NEWNODE(UniOp, AST::ExprNodeUniOp::REF, mv$(v2)))
            ),
            ::make_vec2(::AST::ExprNodeMatchArm(::make_vec1(AST::Pattern(AST::Pattern::TagValue(), sp, get_path(core_name, "cmp", "Ordering", "Equal"))), {}, NEWNODE(Tuple, ::std::vector<AST::ExprNodeP>())), ::AST::ExprNodeMatchArm(::make_vec1(AST::Pattern(AST::Pattern::TagBind(), sp, rcstring_res)), {}, NEWNODE(Flow, AST::ExprNodeFlow::RETURN, "", NEWNODE(NamedValue, AST::Path(rcstring_res)))))
        );
    }

    AST::ExprNodeP equal_value(Span sp, const RcString& core_name) const override {
        return NEWNODE(NamedValue, get_path(core_name, "cmp", "Ordering", "Equal"));
    }

    AST::ExprNodeP enum_mismatch(Span sp, const RcString& core_name) const override {
        return NEWNODE(CallPath, get_path(core_name, "cmp", "Ord", "cmp"), ::make_vec2(NEWNODE(UniOp, AST::ExprNodeUniOp::REF, NEWNODE(CallPath, get_path(core_name, "intrinsics", "discriminant_value"), make_vec1(NEWNODE(NamedValue, AST::Path(rcstring_self))))), NEWNODE(UniOp, AST::ExprNodeUniOp::REF, NEWNODE(CallPath, get_path(core_name, "intrinsics", "discriminant_value"), make_vec1(NEWNODE(NamedValue, AST::Path(rcstring_v)))))));
    }

public:
    const char* trait_name() const override {
        return "Ord";
    }
} g_derive_ord;

class DeriverClone: public Deriver {
    AST::Path get_trait_path(const RcString& core_name) const {
        return AST::Path(core_name, {AST::PathNode(rcstring_clone, {}), AST::PathNode(rcstringClone, {})});
    }

    AST::Path get_method_path(const RcString& core_name) const {
        return get_trait_path(core_name) + rcstring_clone;
    }

    AST::Impl make_ret(Span sp, const RcString& core_name, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> types_to_bound, AST::ExprNodeP node) const {
        const AST::Path trait_path = this->get_trait_path(core_name);

        AST::Function fcn(sp, mktypeSelf(sp), vec$(AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstring_self), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), false, mktypeSelf(sp)))));
        fcn.set_code(NEWNODE(Block, mv$(node)));

        AST::GenericParams params = get_params_with_bounds(sp, p, trait_path, mv$(types_to_bound));

        AST::Impl rv(AST::ImplDef(mv$(params), make_spanned(sp, trait_path), type.clone()));
        rv.add_function(sp, {}, AST::Visibility::make_bare_private(), false, rcstring_clone, mv$(fcn));
        return mv$(rv);
    }

    AST::ExprNodeP clone_val_ref(const RcString& core_name, AST::ExprNodeP val) const {
        // TODO: Hack for zero-sized arrays? (Not a 1.19 feature)
        return NEWNODE(CallPath, this->get_method_path(core_name), vec$(NEWNODE(UniOp, AST::ExprNodeUniOp::REF, mv$(val))));
    }

    AST::ExprNodeP clone_val_direct(const RcString& core_name, AST::ExprNodeP val) const {
        return NEWNODE(CallPath, this->get_method_path(core_name), vec$(mv$(val)));
    }

    AST::ExprNodeP field(const RcString& name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstring_self)), name);
    }

    AST::ExprNodeP field(const ::std::string& name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstring_self)), RcString::new_interned(name));
    }

public:
    const char* trait_name() const override {
        return "Clone";
    }

    AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Struct& str) const override {
        const AST::Path& ty_path = *type.mData.as_Path();

        AST::ExprNodeP node;
        TU_MATCH_HDRA( (str.mData), {)
        TU_ARMA(Unit, e) {
                node = NEWNODE(NamedValue, AST::Path(ty_path));
            }
            TU_ARMA(Struct, e) {
                ::AST::ExprNodeStructLiteral::t_values vals;
                for (const auto& fld : e.ents) {
                    vals.push_back({{}, fld.mName, this->clone_val_ref(opts.core_name, this->field(fld.mName))});
                }
                node = NEWNODE(StructLiteral, ty_path, nullptr, mv$(vals));
            }
            TU_ARMA(Tuple, e) {
                ::std::vector<AST::ExprNodeP> vals;
                for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                    vals.push_back(this->clone_val_ref(opts.core_name, this->field(FMT(idx))));
                }
                node = NEWNODE(CallPath, AST::Path(ty_path), mv$(vals));
            }
        }

        return this->make_ret(sp, opts.core_name, p, type, this->get_field_bounds(str), NEWNODE(Block, mv$(node)));
    }

    AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Enum& enm) const override {
        AST::Path base_path = *type.mData.as_Path();
        base_path.nodes().back().args() = ::AST::PathParams();
        ::std::vector<AST::ExprNodeMatchArm> arms;

        for (const auto& v : enm.variants()) {
            AST::ExprNodeP code;
            AST::Pattern pat_a;

            TU_MATCH_HDRA( (v.mData), {)
            TU_ARMA(Unit, e) {
                    code = NEWNODE(NamedValue, base_path + v.mName);
                    pat_a = AST::Pattern(AST::Pattern::TagValue(), sp, AST::Pattern::Value::make_Named(base_path + v.mName));
                }
                TU_ARMA(Tuple, e) {
                    ::std::vector<AST::Pattern> pats_a;
                    ::std::vector<AST::ExprNodeP> nodes;

                    for (size_t idx = 0; idx < e.mItems.size(); idx++) {
                        auto name_a = RcString::new_interned(FMT("a" << idx));
                        pats_a.push_back(::AST::Pattern(::AST::Pattern::TagBind(), sp, name_a, ::AST::PatternBinding::Type::REF));
                        nodes.push_back(this->clone_val_direct(opts.core_name, NEWNODE(NamedValue, AST::Path(name_a))));
                    }

                    pat_a = AST::Pattern(AST::Pattern::TagNamedTuple(), sp, base_path + v.mName, mv$(pats_a));
                    code = NEWNODE(CallPath, base_path + v.mName, mv$(nodes));
                }
                TU_ARMA(Struct, e) {
                    ::std::vector<AST::StructPatternEntry> pats_a;
                    ::AST::ExprNodeStructLiteral::t_values vals;

                    for (const auto& fld : e.fields) {
                        auto name_a = RcString::new_interned(FMT("a" << fld.mName));
                        pats_a.push_back(AST::StructPatternEntry{AST::AttributeList(), fld.mName, ::AST::Pattern(::AST::Pattern::TagBind(), sp, name_a, ::AST::PatternBinding::Type::REF)});
                        vals.push_back({{}, fld.mName, this->clone_val_direct(opts.core_name, NEWNODE(NamedValue, AST::Path(name_a)))});
                    }

                    pat_a = AST::Pattern(AST::Pattern::TagStruct(), sp, base_path + v.mName, mv$(pats_a), true);
                    code = NEWNODE(StructLiteral, base_path + v.mName, nullptr, mv$(vals));
                }
            }

            ::std::vector< AST::Pattern>    pats;
            pats.push_back( AST::Pattern(AST::Pattern::TagReference(), sp, false, mv$(pat_a)) );

            arms.push_back(AST::ExprNodeMatchArm(
                mv$(pats),
                {},
                mv$(code)
                ));
        }

        return this->make_ret(sp, opts.core_name, p, type, this->get_field_bounds(enm), NEWNODE(Match, NEWNODE(NamedValue, AST::Path(rcstring_self)), mv$(arms)));
    }

    AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Union& unn) const override {
        return make_copy_clone(sp, opts, p, type, this->get_field_bounds(unn));
    }

private:
    AST::Impl make_copy_clone(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> field_bounds) const {
        // Clone on a union can only be a bitwise copy.
        // - This requires a Copy impl. That's up to the user
        auto ret = this->make_ret(sp, opts.core_name, p, type, ::std::move(field_bounds), NEWNODE(Deref, NEWNODE(NamedValue, AST::Path(rcstring_self))));

        // TODO: What if the type is only conditionally copy? (generic over something)
        // - Could abuse specialisation support...
        // TODO: Are these bounds needed?
        for (auto& b : ret.def().params().bounds) {
            auto& be = b.as_IsTrait();
            be.trait = get_path(opts.core_name, "marker", "Copy");
        }

        return ret;
    }
} g_derive_clone;

class DeriverCopy: public Deriver {
    AST::Path get_trait_path(const RcString& core_name) const {
        return get_path(core_name, "marker", "Copy");
    }

    AST::Impl make_ret(Span sp, const RcString& core_name, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> types_to_bound, AST::ExprNodeP node) const {
        const AST::Path trait_path = this->get_trait_path(core_name);

        AST::GenericParams params = get_params_with_bounds(sp, p, trait_path, mv$(types_to_bound));

        AST::Impl rv(AST::ImplDef(mv$(params), make_spanned(sp, trait_path), type.clone()));
        return mv$(rv);
    }

public:
    const char* trait_name() const override {
        return "Copy";
    }

    AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Struct& str) const override {
        return this->make_ret(sp, opts.core_name, p, type, this->get_field_bounds(str), nullptr);
    }

    AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Enum& enm) const override {
        return this->make_ret(sp, opts.core_name, p, type, this->get_field_bounds(enm), nullptr);
    }

    AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Union& unn) const override {
        return this->make_ret(sp, opts.core_name, p, type, this->get_field_bounds(unn), nullptr);
    }
} g_derive_copy;

class DeriverDefault: public Deriver {
    AST::Path get_trait_path(const RcString& core_name) const {
        return get_path(core_name, "default", "Default");
    }

    AST::Path get_method_path(const RcString& core_name) const {
        return AST::Path::new_ufcs_trait(::TypeRef(Span()), get_trait_path(core_name), {AST::PathNode(RcString::new_interned("default"), {})});
    }

    AST::Impl make_ret(Span sp, const RcString& core_name, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> types_to_bound, AST::ExprNodeP node) const {
        const AST::Path trait_path = this->get_trait_path(core_name);

        AST::Function fcn(sp, mktypeSelf(sp), {});
        fcn.set_code(NEWNODE(Block, mv$(node)));

        AST::GenericParams params = get_params_with_bounds(sp, p, trait_path, mv$(types_to_bound));

        AST::Impl rv(AST::ImplDef(mv$(params), make_spanned(sp, trait_path), type.clone()));
        rv.add_function(sp, {}, AST::Visibility::make_bare_private(), false, RcString::new_interned("default"), mv$(fcn));
        return mv$(rv);
    }

    AST::ExprNodeP default_call(const RcString& core_name) const {
        return NEWNODE(CallPath, this->get_method_path(core_name), {});
    }

public:
    const char* trait_name() const override {
        return "Default";
    }

    AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Struct& str) const override {
        const AST::Path& ty_path = *type.mData.as_Path();
        AST::ExprNodeP node;

        TU_MATCH_HDRA( (str.mData), {)
        TU_ARMA(Unit, e) {
                node = NEWNODE(NamedValue, AST::Path(ty_path));
            }
            TU_ARMA(Struct, e) {
                ::AST::ExprNodeStructLiteral::t_values vals;
                bool has_default = false;
                for (const auto& fld : e.ents) {
                    if (fld.defaultValue) {
                        has_default = true;
                    } else {
                        vals.push_back({{}, fld.mName, this->default_call(opts.core_name)});
                    }
                }
                if (has_default) {
                    node = NEWNODE(StructLiteralPattern, ty_path, mv$(vals));
                } else {
                    node = NEWNODE(StructLiteral, ty_path, nullptr, mv$(vals));
                }
            }
            TU_ARMA(Tuple, e) {
                ::std::vector<AST::ExprNodeP> vals;
                for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                    vals.push_back(this->default_call(opts.core_name));
                }
                node = NEWNODE(CallPath, AST::Path(ty_path), mv$(vals));
            }
        }

        return this->make_ret(sp, opts.core_name, p, type, this->get_field_bounds(str), NEWNODE(Block, mv$(node)));
    }

    AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Enum& enm) const override {
        // 1.74: #[default]
        const AST::EnumVariant* default_var = nullptr;
        for (const auto& v : enm.variants()) {
            if (v.mAttrs.has("default")) {
                if (default_var) {
                    ERROR(sp, E0000, "Multiple #[default] attributes");
                }
                default_var = &v;
            }
        }
        if (!default_var) {
            ERROR(sp, E0000, "No #[default] attribute on enum with derive(Default)");
        }

        AST::Path var_path = *type.mData.as_Path() + AST::PathNode(default_var->mName);

        ::std::vector<TypeRef> bound_tys;
        AST::ExprNodeP node;
        TU_MATCH_HDRA( (default_var->mData), { )
        TU_ARMA(Unit, e) {
                node = NEWNODE(NamedValue, std::move(var_path));
            }
            TU_ARMA(Tuple, e) {
                ::std::vector<AST::ExprNodeP> vals;
                for (const auto& fld : e.mItems) {
                    add_field_bound_from_ty(enm.params(), bound_tys, fld.mType);
                    vals.push_back(this->default_call(opts.core_name));
                }
                node = NEWNODE(CallPath, std::move(var_path), mv$(vals));
            }
            TU_ARMA(Struct, e) {
                ::AST::ExprNodeStructLiteral::t_values vals;
                for (const auto& fld : e.fields) {
                    if (fld.defaultValue) {
                    } else {
                        add_field_bound_from_ty(enm.params(), bound_tys, fld.mType);
                        vals.push_back({{}, fld.mName, this->default_call(opts.core_name)});
                    }
                }
                node = NEWNODE(StructLiteralPattern, std::move(var_path), mv$(vals));
            }
        }
        return this->make_ret(sp, opts.core_name, p, type, std::move(bound_tys), std::move(node));
    }
} g_derive_default;

class DeriverHash: public Deriver {
    AST::Path get_trait_path(const RcString& core_name) const {
        return get_path(core_name, "hash", "Hash");
    }

    AST::Path getTraitPathHasher(const RcString& core_name) const {
        return get_path(core_name, "hash", "Hasher");
    }

    AST::Path get_method_path(const RcString& core_name) const {
        return get_trait_path(core_name) + RcString::new_interned("hash");
    }

    AST::Impl make_ret(Span sp, const RcString& core_name, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> types_to_bound, AST::ExprNodeP node) const {
        const AST::Path trait_path = this->get_trait_path(core_name);

        AST::Function fcn(sp, TypeRef(TypeRef::TagUnit(), sp), vec$(AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstring_self), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), false, mktypeSelf(sp))), AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstring_state), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), true, TypeRef(sp, rcstringH, 0x100 | 0)))));
        fcn.params().add_ty_param(AST::TypeParam(sp, {}, rcstringH));
        fcn.params().add_bound(AST::GenericBound::make_IsTrait({sp, {}, TypeRef(sp, rcstringH, 0x100 | 0), {}, this->getTraitPathHasher(core_name)}));
        fcn.set_code(NEWNODE(Block, mv$(node)));

        AST::GenericParams params = get_params_with_bounds(sp, p, trait_path, mv$(types_to_bound));

        AST::Impl rv(AST::ImplDef(mv$(params), make_spanned(sp, trait_path), type.clone()));
        rv.add_function(sp, {}, AST::Visibility::make_bare_private(), false, RcString::new_interned("hash"), mv$(fcn));
        return mv$(rv);
    }

    AST::ExprNodeP hash_val_ref(const RcString& core_name, AST::ExprNodeP val) const {
        return this->hash_val_direct(core_name, NEWNODE(UniOp, AST::ExprNodeUniOp::REF, mv$(val)));
    }

    AST::ExprNodeP hash_val_direct(const RcString& core_name, AST::ExprNodeP val) const {
        return NEWNODE(CallPath, this->get_method_path(core_name), vec$(mv$(val), NEWNODE(NamedValue, AST::Path(rcstring_state))));
    }

    AST::ExprNodeP field(const RcString& name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstring_self)), name);
    }

    AST::ExprNodeP field(const std::string& name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstring_self)), RcString::new_interned(name));
    }

public:
    const char* trait_name() const override {
        return "Hash";
    }

    AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Struct& str) const override {
        auto block = new_block(sp);

        TU_MATCH_HDRA( (str.mData), {)
        TU_ARMA(Unit, e) {
            }
            TU_ARMA(Struct, e) {
                for (const auto& fld : e.ents) {
                    block->push_stmt(this->hash_val_ref(opts.core_name, this->field(fld.mName)));
                }
            }
            TU_ARMA(Tuple, e) {
                for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                    block->push_stmt(this->hash_val_ref(opts.core_name, this->field(FMT(idx))));
                }
            }
        }

        return this->make_ret(sp, opts.core_name, p, type, this->get_field_bounds(str), mk_exprnodep(block.release()));
    }

    AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Enum& enm) const override {
        AST::Path base_path = *type.mData.as_Path();
        base_path.nodes().back().args() = ::AST::PathParams();
        ::std::vector<AST::ExprNodeMatchArm> arms;

        for (unsigned int var_idx = 0; var_idx < enm.variants().size(); var_idx++) {
            const auto& v = enm.variants()[var_idx];
            AST::Pattern pat_a;

            auto var_path = base_path + v.mName;
            auto var_idx_hash = enm.variants().size() > 1 ? this->hash_val_ref(opts.core_name, NEWNODE(Integer, U128(var_idx), CORETYPE_UINT)) : NEWNODE(Tuple, {});

            auto block = new_block(sp);
            block->push_stmt(mv$(var_idx_hash));
            TU_MATCH_HDRA( (v.mData), {)
            TU_ARMA(Unit, e) {
                    pat_a = AST::Pattern(AST::Pattern::TagValue(), sp, AST::Pattern::Value::make_Named(var_path));
                }
                TU_ARMA(Tuple, e) {
                    ::std::vector<AST::Pattern> pats_a;
                    make_refpat_a(sp, *block, pats_a, e.mItems, [&](size_t, auto a) {
                        return this->hash_val_direct(opts.core_name, mv$(a));
                    });
                    pat_a = AST::Pattern(AST::Pattern::TagNamedTuple(), sp, var_path, mv$(pats_a));
                }
                TU_ARMA(Struct, e) {
                    ::std::vector<AST::StructPatternEntry> pats_a;
                    make_refpat_a(sp, *block, pats_a, e.fields, [&](size_t, auto a) {
                        return this->hash_val_direct(opts.core_name, mv$(a));
                    });
                    pat_a = AST::Pattern(AST::Pattern::TagStruct(), sp, var_path, mv$(pats_a), true);
                }
            }

            ::std::vector< AST::Pattern>    pats;
            pats.push_back( AST::Pattern(AST::Pattern::TagReference(), sp, false, mv$(pat_a)) );

            arms.push_back(AST::ExprNodeMatchArm(
                mv$(pats),
                {},
                mk_exprnodep(block.release())
                ));
        }

        return this->make_ret(sp, opts.core_name, p, type, this->get_field_bounds(enm), NEWNODE(Match, NEWNODE(NamedValue, AST::Path(rcstring_self)), mv$(arms)));
    }
} g_derive_hash;

class DeriverRustcEncodable: public Deriver {
    // NOTE: This emits paths like `::rustc_serialize::Encodable` - rustc and crates.io have subtly different crate names
    AST::Path get_trait_path() const {
        return AST::Path(RcString::new_interned("=rustc_serialize"), {AST::PathNode(RcString::new_interned("Encodable"), {})});
    }

    AST::Path getTraitPathEncoder() const {
        return AST::Path(RcString::new_interned("=rustc_serialize"), {AST::PathNode(RcString::new_interned("Encoder"), {})});
    }

    AST::Path get_method_path() const {
        return get_trait_path() + "encode";
    }

    AST::Impl make_ret(Span sp, const RcString& core_name, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> types_to_bound, AST::ExprNodeP node) const {
        const AST::Path trait_path = this->get_trait_path();

        AST::Path result_path = get_path(core_name, "result", "Result");
        result_path.nodes()[1].args().entries.push_back(TypeRef(TypeRef::TagUnit(), sp));
        result_path.nodes()[1].args().entries.push_back(TypeRef(sp, AST::Path::new_ufcs_trait(TypeRef(sp, "S", 0x100 | 0), this->getTraitPathEncoder(), {AST::PathNode("Error", {})})));

        AST::Function fcn(sp, TypeRef(sp, mv$(result_path)), vec$(AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstring_self), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), false, mktypeSelf(sp))), AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstring_s), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), true, TypeRef(sp, RcString::new_interned("S"), 0x100 | 0)))));
        fcn.params().add_ty_param(AST::TypeParam(sp, {}, "S"));
        fcn.params().add_bound(AST::GenericBound::make_IsTrait({sp, {}, TypeRef(sp, "S", 0x100 | 0), {}, this->getTraitPathEncoder()}));
        fcn.set_code(NEWNODE(Block, mv$(node)));

        AST::GenericParams params = get_params_with_bounds(sp, p, trait_path, mv$(types_to_bound));

        AST::Impl rv(AST::ImplDef(mv$(params), make_spanned(sp, trait_path), type.clone()));
        rv.add_function(sp, {}, AST::Visibility::make_bare_private(), false, "encode", mv$(fcn));
        return mv$(rv);
    }

    AST::ExprNodeP enc_val_direct(AST::ExprNodeP val) const {
        return NEWNODE(CallPath, this->get_method_path(), vec$(mv$(val), NEWNODE(NamedValue, AST::Path(rcstring_s))));
    }

    AST::ExprNodeP enc_val_ref(AST::ExprNodeP val) const {
        return this->enc_val_direct(NEWNODE(UniOp, AST::ExprNodeUniOp::REF, mv$(val)));
    }

    AST::ExprNodeP field(const RcString& name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstring_self)), name);
    }

    AST::ExprNodeP field(::std::string name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstring_self)), RcString::new_interned(name));
    }

    AST::ExprNodeP enc_closure(Span sp, AST::ExprNodeP code) const {
        return NEWNODE(Closure, vec$(::std::make_pair(AST::Pattern(AST::Pattern::TagBind(), sp, rcstring_s), ::TypeRef(sp))), ::TypeRef(sp), mv$(code), false, false);
    }

    AST::ExprNodeP get_val_ok(const RcString& core_name) const {
        return NEWNODE(CallPath, get_path(core_name, "result", "Result", "Ok"), vec$(NEWNODE(Tuple, {})));
    }

public:
    const char* trait_name() const override {
        return "RustcEncodable";
    }

    AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Struct& str) const override {
        ::std::string struct_name = type.mData.as_Path()->nodes().back().name().c_str();

        auto block = new_block(sp);
        TU_MATCH_HDRA( (str.mData), {)
        TU_ARMA(Unit, e) {
            }
            TU_ARMA(Struct, e) {
                unsigned int idx = 0;
                for (const auto& fld : e.ents) {
                    block->push_stmt(NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_struct_field", vec$(NEWNODE(NamedValue, AST::Path(rcstring_s)), NEWNODE(String, fld.mName.c_str()), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->enc_closure(sp, this->enc_val_ref(this->field(fld.mName))))));
                    idx++;
                }
            }
            TU_ARMA(Tuple, e) {
                for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                    block->push_stmt(NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_tuple_struct_arg", vec$(NEWNODE(NamedValue, AST::Path(rcstring_s)), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->enc_closure(sp, this->enc_val_ref(this->field(FMT(idx)))))));
                }
            }
        }

        block->push_tail_expr( this->get_val_ok(opts.core_name) );
        auto closure = this->enc_closure( sp, mk_exprnodep(block.release()) );

        ::AST::ExprNodeP    node;
        TU_MATCH_HDRA( (str.mData), {)
        TU_ARMA(Unit, e) {
                node = get_val_ok(opts.core_name);
            }
            TU_ARMA(Struct, e) {
                node = NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_struct", vec$(NEWNODE(NamedValue, AST::Path(rcstring_s)), NEWNODE(String, struct_name), NEWNODE(Integer, U128(e.ents.size()), CORETYPE_UINT), mv$(closure)));
            }
            TU_ARMA(Tuple, e) {
                node = NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_tuple_struct", vec$(NEWNODE(NamedValue, AST::Path(rcstring_s)), NEWNODE(String, struct_name), NEWNODE(Integer, U128(e.ents.size()), CORETYPE_UINT), mv$(closure)));
            }
        }

        return this->make_ret(sp, opts.core_name, p, type, this->get_field_bounds(str), mv$(node));
    }

    AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Enum& enm) const override {
        AST::Path base_path = *type.mData.as_Path();
        base_path.nodes().back().args() = ::AST::PathParams();
        ::std::vector<AST::ExprNodeMatchArm> arms;

        auto s_ent = NEWNODE(NamedValue, AST::Path(rcstring_s));

        for (unsigned int var_idx = 0; var_idx < enm.variants().size(); var_idx++) {
            const auto& v = enm.variants()[var_idx];
            AST::ExprNodeP code;
            AST::Pattern pat_a;

            TU_MATCH_HDRA((v.mData), {)
            TU_ARMA(Unit, e) {
                    code = NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_enum_variant", vec$(s_ent->clone(), NEWNODE(String, v.mName.c_str()), NEWNODE(Integer, U128(var_idx), CORETYPE_UINT), NEWNODE(Integer, U128(0), CORETYPE_UINT), this->enc_closure(sp, this->get_val_ok(opts.core_name))));
                    pat_a = AST::Pattern(AST::Pattern::TagValue(), sp, AST::Pattern::Value::make_Named(base_path + v.mName));
                }
                TU_ARMA(Tuple, e) {
                    auto block = new_block(sp);
                    ::std::vector<AST::Pattern> pats_a;
                    make_refpat_a(sp, *block, pats_a, e.mItems, [&](size_t idx, auto a) {
                        return NEWNODE(CallPath, this->getTraitPathEncoder() + RcString::new_interned("emit_enum_variant_arg"), vec$(s_ent->clone(), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->enc_closure(sp, this->enc_val_direct(mv$(a)))));
                    });
                    block->push_tail_expr(this->get_val_ok(opts.core_name));

                    code = NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_enum_variant", vec$(s_ent->clone(), NEWNODE(String, v.mName.c_str()), NEWNODE(Integer, U128(var_idx), CORETYPE_UINT), NEWNODE(Integer, U128(e.mItems.size()), CORETYPE_UINT), this->enc_closure(sp, mk_exprnodep(block.release()))));
                    pat_a = AST::Pattern(AST::Pattern::TagNamedTuple(), sp, base_path + v.mName, mv$(pats_a));
                }
                TU_ARMA(Struct, e) {
                    auto block = new_block(sp);
                    ::std::vector<AST::StructPatternEntry> pats_a;
                    make_refpat_a(sp, *block, pats_a, e.fields, [&](size_t idx, auto a) {
                        return NEWNODE(CallPath, this->getTraitPathEncoder() + RcString::new_interned("emit_enum_struct_variant_field"), vec$(s_ent->clone(), NEWNODE(String, e.fields[idx].mName.c_str()), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->enc_closure(sp, this->enc_val_direct(mv$(a)))));
                    });
                    block->push_tail_expr(this->get_val_ok(opts.core_name));

                    pat_a = AST::Pattern(AST::Pattern::TagStruct(), sp, base_path + v.mName, mv$(pats_a), true);
                    code = NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_enum_struct_variant", vec$(s_ent->clone(), NEWNODE(String, v.mName.c_str()), NEWNODE(Integer, U128(var_idx), CORETYPE_UINT), NEWNODE(Integer, U128(e.fields.size()), CORETYPE_UINT), this->enc_closure(sp, mk_exprnodep(block.release()))));
                }
            }

            ::std::vector< AST::Pattern>    pats;
            pats.push_back( AST::Pattern(AST::Pattern::TagReference(), sp, false, mv$(pat_a)) );

            arms.push_back(AST::ExprNodeMatchArm(
                mv$(pats),
                {},
                mv$(code)
                ));
        }

        auto node_match = NEWNODE(Match, NEWNODE(NamedValue, AST::Path(rcstring_self)), mv$(arms));

        ::std::string enum_name = type.mData.as_Path()->nodes().back().name().c_str();
        auto node = NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_enum", vec$(mv$(s_ent), NEWNODE(String, enum_name), this->enc_closure(sp, mv$(node_match))));

        return this->make_ret(sp, opts.core_name, p, type, this->get_field_bounds(enm), mv$(node));
    }
} g_derive_rustc_encodable;

class DeriverRustcDecodable: public Deriver {
    // NOTE: This emits paths like `::rustc_serialize::Encodable` - rustc and crates.io have subtly different crate names
    AST::Path get_trait_path() const {
        return AST::Path(RcString::new_interned("=rustc_serialize"), {AST::PathNode(RcString::new_interned("Decodable"), {})});
    }

    AST::Path getTraitPathDecoder() const {
        return AST::Path(RcString::new_interned("=rustc_serialize"), {AST::PathNode(RcString::new_interned("Decoder"), {})});
    }

    AST::Path get_method_path() const {
        return get_trait_path() + "decode";
    }

    AST::Impl make_ret(Span sp, const RcString& core_name, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> types_to_bound, AST::ExprNodeP node) const {
        const AST::Path trait_path = this->get_trait_path();

        AST::Path result_path = get_path(core_name, "result", "Result");
        result_path.nodes()[1].args().entries.push_back(mktypeSelf(sp));
        result_path.nodes()[1].args().entries.push_back(TypeRef(sp, AST::Path::new_ufcs_trait(TypeRef(sp, "D", 0x100 | 0), this->getTraitPathDecoder(), {AST::PathNode("Error", {})})));

        AST::Function fcn(
            sp,
            TypeRef(sp, result_path),
            vec$(
                //AST::Function::Arg( AST::Pattern(AST::Pattern::TagBind(), sp, rcstring_self), TypeRef(TypeRef::TagReference(), sp, false, AST::LifetimeRef(), mktype_Self(sp)) ),
                AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, "d"), TypeRef(TypeRef::TagReference(), sp, AST::LifetimeRef(), true, TypeRef(sp, "D", 0x100 | 0)))
            )
        );
        fcn.params().add_ty_param(AST::TypeParam(sp, {}, "D"));
        fcn.params().add_bound(AST::GenericBound::make_IsTrait({sp, {}, TypeRef(sp, "D", 0x100 | 0), {}, this->getTraitPathDecoder()}));
        fcn.set_code(NEWNODE(Block, mv$(node)));

        AST::GenericParams params = get_params_with_bounds(sp, p, trait_path, mv$(types_to_bound));

        AST::Impl rv(AST::ImplDef(mv$(params), make_spanned(sp, trait_path), type.clone()));
        rv.add_function(sp, {}, AST::Visibility::make_bare_private(), false, "decode", mv$(fcn));
        return mv$(rv);
    }

    AST::ExprNodeP dec_val() const {
        return NEWNODE(CallPath, this->get_method_path(), vec$(NEWNODE(NamedValue, AST::Path("d"))));
    }

    AST::ExprNodeP field(const ::std::string& name) const {
        return NEWNODE(Field, NEWNODE(NamedValue, AST::Path(rcstring_self)), RcString::new_interned(name));
    }

    AST::ExprNodeP dec_closure(Span sp, AST::ExprNodeP code) const {
        return NEWNODE(Closure, vec$(::std::make_pair(AST::Pattern(AST::Pattern::TagBind(), sp, "d"), ::TypeRef(sp))), ::TypeRef(sp), mv$(code), false, false);
    }

    AST::ExprNodeP get_val_err_str(const RcString& core_name, ::std::string err_str) const {
        return NEWNODE(CallPath, get_path(core_name, "result", "Result", "Err"), vec$(NEWNODE(CallMethod, NEWNODE(NamedValue, AST::Path("d")), AST::PathNode("error"), vec$(NEWNODE(String, err_str)))));
    }

    AST::ExprNodeP get_val_ok(const RcString& core_name, AST::ExprNodeP inner) const {
        return NEWNODE(CallPath, get_path(core_name, "result", "Result", "Ok"), vec$(mv$(inner)));
    }

    AST::ExprNodeP get_val_ok_unit(const RcString& core_name) const {
        return get_val_ok(core_name, NEWNODE(Tuple, {}));
    }

public:
    const char* trait_name() const override {
        return "RustcDecodable";
    }

    AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Struct& str) const override {
        AST::Path base_path = *type.mData.as_Path();
        ::std::string struct_name = base_path.nodes().back().name().c_str();

        AST::ExprNodeP node_v;
        TU_MATCH_HDRA((str.mData), {)
        TU_ARMA(Unit, e) {
            }
            TU_ARMA(Struct, e) {
                ::AST::ExprNodeStructLiteral::t_values vals;
                unsigned int idx = 0;
                for (const auto& fld : e.ents) {
                    vals.push_back({{}, fld.mName, NEWNODE(UniOp, ::AST::ExprNodeUniOp::QMARK, NEWNODE(CallPath, this->getTraitPathDecoder() + "read_struct_field", vec$(NEWNODE(NamedValue, AST::Path("d")), NEWNODE(String, fld.mName.c_str()), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->dec_closure(sp, this->dec_val()))))});
                    idx++;
                }
                node_v = NEWNODE(StructLiteral, base_path, nullptr, mv$(vals));
            }
            TU_ARMA(Tuple, e) {
                ::std::vector<AST::ExprNodeP> vals;
                for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                    vals.push_back(NEWNODE(UniOp, ::AST::ExprNodeUniOp::QMARK, NEWNODE(CallPath, this->getTraitPathDecoder() + "read_tuple_struct_arg", vec$(NEWNODE(NamedValue, AST::Path("d")), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->dec_closure(sp, this->dec_val())))));
                }
                node_v = NEWNODE(CallPath, mv$(base_path), mv$(vals));
            }
        }

        auto closure = this->dec_closure( sp, this->get_val_ok(opts.core_name, mv$(node_v)) );

        auto args = vec$( NEWNODE(NamedValue, AST::Path("d")), NEWNODE(String, struct_name), AST::ExprNodeP(), mv$(closure) );

        ::AST::ExprNodeP    node;
        TU_MATCH_HDRA((str.mData), {)
        TU_ARMA(Unit, e) {
                node = this->get_val_ok(opts.core_name, NEWNODE(NamedValue, mv$(base_path)));
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

        return this->make_ret(sp, opts.core_name, p, type, this->get_field_bounds(str), mv$(node));
    }

    AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Enum& enm) const override {
        AST::Path base_path = *type.mData.as_Path();
        base_path.nodes().back().args() = ::AST::PathParams();
        ::std::vector<AST::ExprNodeMatchArm> arms;

        // 1. Variant names
        ::std::vector<AST::ExprNodeP> var_name_strs;

        // 2. Decoding arms
        for (unsigned int var_idx = 0; var_idx < enm.variants().size(); var_idx++) {
            const auto& v = enm.variants()[var_idx];
            AST::ExprNodeP code;

            TU_MATCH_HDRA( (v.mData), {)
            TU_ARMA(Unit, e) {
                    code = NEWNODE(NamedValue, base_path + v.mName);
                }
                TU_ARMA(Tuple, e) {
                    ::std::vector<AST::ExprNodeP> args;

                    for (unsigned int idx = 0; idx < e.mItems.size(); idx++) {
                        args.push_back(NEWNODE(UniOp, ::AST::ExprNodeUniOp::QMARK, NEWNODE(CallPath, this->getTraitPathDecoder() + "read_enum_variant_arg", vec$(NEWNODE(NamedValue, AST::Path("d")), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->dec_closure(sp, this->dec_val())))));
                    }
                    code = NEWNODE(CallPath, base_path + v.mName, mv$(args));
                }
                TU_ARMA(Struct, e) {
                    ::AST::ExprNodeStructLiteral::t_values vals;

                    unsigned int idx = 0;
                    for (const auto& fld : e.fields) {
                        vals.push_back({{}, fld.mName, NEWNODE(UniOp, ::AST::ExprNodeUniOp::QMARK, NEWNODE(CallPath, this->getTraitPathDecoder() + "read_enum_struct_variant_field", vec$(NEWNODE(NamedValue, AST::Path("d")), NEWNODE(String, fld.mName.c_str()), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->dec_closure(sp, this->dec_val()))))});
                        idx++;
                    }

                    code = NEWNODE(StructLiteral, base_path + v.mName, nullptr, mv$(vals));
                }
            }

            ::std::vector< AST::Pattern>    pats;
            pats.push_back( AST::Pattern(AST::Pattern::TagValue(), sp, AST::Pattern::Value::make_Integer({CORETYPE_UINT, U128(var_idx)})) );

            arms.push_back(AST::ExprNodeMatchArm(
                mv$(pats),
                {},
                this->get_val_ok(opts.core_name, mv$(code))
                ));
            var_name_strs.push_back( NEWNODE(String, v.mName.c_str()) );
        }

        // Default arm
        {
            arms.push_back(AST::ExprNodeMatchArm(::make_vec1(AST::Pattern()), {}, this->get_val_err_str(opts.core_name, "enum value unknown")));
        }

        auto node_match = NEWNODE(Match, NEWNODE(NamedValue, AST::Path("idx")), mv$(arms));
        auto node_var_closure = NEWNODE(Closure, vec$(::std::make_pair(AST::Pattern(AST::Pattern::TagBind(), sp, "d"), ::TypeRef(sp)), ::std::make_pair(AST::Pattern(AST::Pattern::TagBind(), sp, "idx"), ::TypeRef(sp))), ::TypeRef(sp), mv$(node_match), false, false);
        ::std::string enum_name = type.mData.as_Path()->nodes().back().name().c_str();

        auto node_rev = NEWNODE(CallPath, this->getTraitPathDecoder() + "read_enum_variant", vec$(NEWNODE(NamedValue, AST::Path("d")), NEWNODE(UniOp, AST::ExprNodeUniOp::REF, NEWNODE(Array, mv$(var_name_strs))), mv$(node_var_closure)));

        auto node = NEWNODE(CallPath, this->getTraitPathDecoder() + "read_enum", vec$(NEWNODE(NamedValue, AST::Path("d")), NEWNODE(String, enum_name), this->dec_closure(sp, mv$(node_rev))));

        return this->make_ret(sp, opts.core_name, p, type, this->get_field_bounds(enm), mv$(node));
    }
} g_derive_rustc_decodable;

class DeriverConstParamTy: public Deriver {
    AST::Impl handle_generic(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, ::std::vector<TypeRef> types_to_bound) const {
        const AST::Path trait_path = get_path(opts.core_name, "marker", "StructuralPartialEq");
        AST::GenericParams params = get_params_with_bounds(sp, p, trait_path, mv$(types_to_bound));
        AST::Impl rv(AST::ImplDef(mv$(params), make_spanned(sp, trait_path), type.clone()));
        return mv$(rv);
    }

public:
    const char* trait_name() const override {
        return "ConstParamTy";
    }

    AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Struct& str) const override {
        return handle_generic(sp, opts, p, type, this->get_field_bounds(str));
    }

    AST::Impl handle_item(Span sp, const DeriveOpts& opts, const AST::GenericParams& p, const TypeRef& type, const AST::Enum& enm) const override {
        return handle_generic(sp, opts, p, type, this->get_field_bounds(enm));
    }
} g_derive_const_param_ty;

// --------------------------------------------------------------------
// Select and dispatch the correct derive() handler
// --------------------------------------------------------------------
static const Deriver* find_impl(const RcString& trait_name) {
#define _(obj)                          \
    if (trait_name == obj.trait_name()) \
        return &obj;
    _(g_derive_debug)
    _(g_derive_partialeq)
    _(g_derive_partialord)
    _(g_derive_eq)
    _(g_derive_ord)
    _(g_derive_clone)
    _(g_derive_copy)
    _(g_derive_default)
    _(g_derive_hash)
    _(g_derive_rustc_encodable)
    _(g_derive_rustc_decodable)
    _(g_derive_const_param_ty)
#undef _
    return nullptr;
}

namespace {
    std::vector<AST::Path> get_derive_items(const AST::Attribute& attr) {
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
                const auto& ty = tok.frag_type();
                ASSERT_BUG(lex.point_span(), ty.is_path(), "TODO: No path :ty in derive, " << ty);
                ASSERT_BUG(lex.point_span(), ty.mData.as_Path(), "" << ty);
                rv.push_back(*ty.mData.as_Path());
            } else {
                auto item = AST::Path::new_relative({}, {});
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

    TypeRef make_type(const Span& sp, const AST::AbsolutePath& path, const AST::GenericParams& params) {
        TypeRef type(sp, path);
        auto& types_args = type.path().nodes().back().args();
        for (const auto& param : params.mParams) {
            if (const auto* pe = param.opt_Type()) {
                types_args.entries.push_back(TypeRef(TypeRef::TagArg(), sp, pe->name()));
            }
            if (const auto* pe = param.opt_Value()) {
                auto p = AST::Path(pe->name().name);
                types_args.entries.push_back(AST::ExprNodeP(new AST::ExprNodeNamedValue(std::move(p))));
            }
        }
        return type;
    }

    std::vector<RcString> find_macro(const Span& sp, const AST::Crate& crate, const AST::Module& mod, const AST::Path& trait_path) {
        std::vector<RcString> mac_path;

        if (trait_path.is_trivial()) {
            //auto mac_name = RcString::new_interned( FMT("derive#" << trait.name().elems.back()) );
            auto mac_name = trait_path.as_trivial();

            for (const auto& mac_import : mod.macroImports) {
                if (mac_import.name == mac_name) {
                    TU_MATCH_HDRA( (mac_import.ref), {)
                    default:
                        break;
                        TU_ARMA(ExternalProcMacro, pm) {
                            DEBUG("proc_macro " << pm->path);
                            mac_path.push_back(pm->path.crate_name());
                            mac_path.insert(mac_path.end(), pm->path.components().begin(), pm->path.components().end());
                        }
                    }
                    if( !mac_path.empty() ) {
                        break;
                    }
                }
            }
        }
        if (mac_path.empty()) {
            auto mac = ExpandLookupMacro(sp, crate, LList<const AST::Module*>(nullptr, &mod), trait_path);

            TU_MATCH_HDRA( (mac), {)
            TU_ARMA(None, e) {
                    // Leave `mac_path` empty, triggering an error in caller
                }
                TU_ARMA(ExternalProcMacro, ext_proc_mac) {
                    mac_path.push_back(ext_proc_mac->path.crate_name());
                    mac_path.insert(mac_path.end(), ext_proc_mac->path.components().begin(), ext_proc_mac->path.components().end());
                }
                TU_ARMA(BuiltinProcMacro, proc_mac) {
                    TODO(sp, "Handle builtin proc macro");
                }
                TU_ARMA(MacroRules, mr_ptr) {
                    TODO(sp, "Custom derive using macro_rules?");
                }
            }
        }
        return mac_path;
    }
}

template <typename T>
static void derive_item(const Span& sp, const AST::Crate& crate, AST::Module& mod, const AST::Attribute& attr, const AST::AbsolutePath& path, slice<const AST::Attribute> attrs, const AST::Visibility& vis, const T& item) {
    auto derive_items = get_derive_items(attr);
    if (derive_items.empty()) {
        //ERROR(sp, E0000, "#[derive()] requires a list of known traits to derive");
        return;
    }

    DEBUG("path = " << path);

    auto type = make_type(sp, path, item.params());

    DeriveOpts opts = {crate.extCratenameCore};

    ::std::vector<AST::Path> missing_handlers;
    for (const auto& trait_path : derive_items) {
        DEBUG("- " << trait_path);

        if (trait_path.is_trivial()) {
            auto dp = find_impl(trait_path.as_trivial());
            if (dp) {
                mod.add_item(sp, AST::Visibility::make_bare_private(), "", dp->handle_item(sp, opts, item.params(), type, item), {});
                continue;
            }
        }

        // TODO: Handle full paths to standard library traits

        std::vector<RcString> mac_path = find_macro(sp, crate, mod, trait_path);
        if (!mac_path.empty()) {
            auto lex = ProcMacroInvoke(sp, crate, mac_path, attrs, vis, path.nodes.back(), item);
            if (lex) {
                lex->parse_state().module = &mod;
                ParseModRootItems(*lex, mod);
            } else {
                ERROR(sp, E0000, "proc_macro derive failed");
            }
            continue;
        }

        // HACK! If the trait path is for `=core` and the last component passes `find_impl`, then assume it's a proper path
        // Some crates spell builtin derives as fully-qualified `::core` paths.

        // Absolute path
        if (const auto* ap = trait_path.cls.opt_Absolute()) {
            // For `::core` (encoded as `=core` due to how it's parsed in `get_derive_items`)
            if (ap->crate == "=core") {
                // And if the last node (ignore intermediate nodes) returns a valid builtin
                if (auto dp = find_impl(ap->nodes.back().name())) {
                    // Use that
                    mod.add_item(sp, AST::Visibility::make_bare_private(), "", dp->handle_item(sp, opts, item.params(), type, item), {});
                    continue;
                }
            }
        }

        DEBUG("> No handler for " << trait_path);
        missing_handlers.push_back(trait_path);
    }

    if (!missing_handlers.empty()) {
        ERROR(sp, E0000, "Failed to apply #[derive] - Missing handlers for " << missing_handlers);
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
    bool wants_all_attrs() const override {
        return true;
    }

    void handle(const Span& sp, const AST::Attribute& attr, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t mod_idx, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        TU_MATCH_DEF(
            ::AST::Item,
            (i),
            (e),
            (TODO(sp, "Handle #[derive] for other item types - " << i.tag_str());),
            (
                None,
                // Ignore, it's been deleted
            ),
            (Union, derive_item(sp, crate, mod, attr, path, attrs, vis, e);),
            (Enum, derive_item(sp, crate, mod, attr, path, attrs, vis, e);),
            (Struct, derive_item(sp, crate, mod, attr, path, attrs, vis, e);)
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
    typedef void (*cb_t)(const Span& sp, AST::Crate& crate, const std::string&, const AST::AbsolutePath&);
    eItemType type;
    cb_t cb;

    Handler(eItemType type, cb_t cb)
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

static std::map<const char*, Handler, StrcmpTy> g_handlers;

void handle_save(const Span& sp, AST::Crate& crate, const std::string& name, const AST::AbsolutePath& path) {
    auto rv = crate.mLangItems.insert(::std::make_pair(name, path));
    if (!rv.second) {
        const auto& other_path = rv.first->second;
        if (path != other_path) {
            // HACK: Anon modules get visited twice, so can lead to duplicate annotations
            ERROR(sp, E0000, "Duplicate definition of language item '" << name << "' - " << other_path << " and " << path);
        }
    } else {
        DEBUG("Bind '" << name << "' to " << path);
    }
}

void handle_lang_item(const Span& sp, AST::Crate& crate, const AST::AbsolutePath& path, const ::std::string& name, eItemType type, AST::Item& item) {
    if (g_handlers.empty()) {
        struct H {
            static void add(const char* n, Handler h) {
                g_handlers.insert(std::make_pair(n, std::move(h)));
            }
        };

        H::add("phantom_fn", Handler(ITEM_FN, handle_save));
        H::add("send", Handler(ITEM_TRAIT, handle_save));
        H::add("sync", Handler(ITEM_TRAIT, handle_save));
        H::add("sized", Handler(ITEM_TRAIT, handle_save));
        H::add("copy", Handler(ITEM_TRAIT, handle_save));
        {
            H::add("clone", Handler(ITEM_TRAIT, handle_save));
        }
        // ops traits
        H::add("drop", Handler(ITEM_TRAIT, handle_save));
        H::add("add", Handler(ITEM_TRAIT, handle_save));
        H::add("sub", Handler(ITEM_TRAIT, handle_save));
        H::add("mul", Handler(ITEM_TRAIT, handle_save));
        H::add("div", Handler(ITEM_TRAIT, handle_save));
        H::add("rem", Handler(ITEM_TRAIT, handle_save));

        H::add("neg", Handler(ITEM_TRAIT, handle_save));
        H::add("not", Handler(ITEM_TRAIT, handle_save));

        H::add("bitand", Handler(ITEM_TRAIT, handle_save));
        H::add("bitor", Handler(ITEM_TRAIT, handle_save));
        H::add("bitxor", Handler(ITEM_TRAIT, handle_save));
        H::add("shl", Handler(ITEM_TRAIT, handle_save));
        H::add("shr", Handler(ITEM_TRAIT, handle_save));

        H::add("add_assign", Handler(ITEM_TRAIT, handle_save));
        H::add("sub_assign", Handler(ITEM_TRAIT, handle_save));
        H::add("div_assign", Handler(ITEM_TRAIT, handle_save));
        H::add("rem_assign", Handler(ITEM_TRAIT, handle_save));
        H::add("mul_assign", Handler(ITEM_TRAIT, handle_save));
        H::add("bitand_assign", Handler(ITEM_TRAIT, handle_save));
        H::add("bitor_assign", Handler(ITEM_TRAIT, handle_save));
        H::add("bitxor_assign", Handler(ITEM_TRAIT, handle_save));
        H::add("shl_assign", Handler(ITEM_TRAIT, handle_save));
        H::add("shr_assign", Handler(ITEM_TRAIT, handle_save));

        H::add("index", Handler(ITEM_TRAIT, handle_save));
        H::add("deref", Handler(ITEM_TRAIT, handle_save));
        H::add("index_mut", Handler(ITEM_TRAIT, handle_save));
        H::add("deref_mut", Handler(ITEM_TRAIT, handle_save));
        H::add("fn", Handler(ITEM_TRAIT, handle_save));
        H::add("fn_mut", Handler(ITEM_TRAIT, handle_save));
        H::add("fn_once", Handler(ITEM_TRAIT, handle_save));

        H::add("eq", Handler(ITEM_TRAIT, handle_save));
        H::add("ord", Handler(ITEM_TRAIT, handle_save)); // In 1.29 this is Ord, before it was PartialOrd
        {
            H::add("partial_ord", Handler(ITEM_TRAIT, handle_save)); // New name for v1.29
        }

        H::add("unsize", Handler(ITEM_TRAIT, handle_save));
        H::add("coerce_unsized", Handler(ITEM_TRAIT, handle_save));
        H::add("freeze", Handler(ITEM_TRAIT, handle_save)); // TODO: What version?

        H::add("iterator", Handler(ITEM_TRAIT, handle_save));    /* mrustc just desugars? */
        H::add("debug_trait", Handler(ITEM_TRAIT, handle_save)); /* TODO: Poke derive() with this */

        {
            H::add("termination", Handler(ITEM_TRAIT, handle_save)); // 1.29 - trait used for non-() main
        }

        {
            H::add("pointee_trait", Handler(ITEM_TRAIT, handle_save));     // 1.54 - pointer metadata trait
            H::add("dyn_metadata", Handler(ITEM_STRUCT, handle_save));     // 1.54 - `dyn Trait` metadata structure
            H::add("structural_peq", Handler(ITEM_TRAIT, handle_save));    // 1.54 - Structural equality trait (partial)
            H::add("structural_teq", Handler(ITEM_TRAIT, handle_save));    // 1.54 - Structural equality trait (total)
            H::add("discriminant_kind", Handler(ITEM_TRAIT, handle_save)); // 1.54 - trait: used for the `discriminant_kind` intrinsic
        }

        H::add("non_zero", Handler(ITEM_STRUCT, handle_save));
        H::add("phantom_data", Handler(ITEM_STRUCT, handle_save));

        {
            H::add("RangeFull", Handler(ITEM_STRUCT, [](const auto& sp, auto& crate, const auto&, const auto& p) {
                handle_save(sp, crate, "range_full", p);
            }));
            H::add("Range", Handler(ITEM_STRUCT, [](const auto& sp, auto& crate, const auto&, const auto& p) {
                handle_save(sp, crate, "range", p);
            }));
            H::add("RangeFrom", Handler(ITEM_STRUCT, [](const auto& sp, auto& crate, const auto&, const auto& p) {
                handle_save(sp, crate, "range_from", p);
            }));
            H::add("RangeTo", Handler(ITEM_STRUCT, [](const auto& sp, auto& crate, const auto&, const auto& p) {
                handle_save(sp, crate, "range_to", p);
            }));
            H::add("RangeInclusive", Handler(ITEM_STRUCT, [](const auto& sp, auto& crate, const auto&, const auto& p) {
                handle_save(sp, crate, "range_inclusive", p);
            }));
            H::add("RangeToInclusive", Handler(ITEM_STRUCT, [](const auto& sp, auto& crate, const auto&, const auto& p) {
                handle_save(sp, crate, "range_to_inclusive", p);
            }));
        }

        {
            H::add("unwind_safe", Handler(ITEM_TRAIT, handle_save));     // 1.54 - UnwindSafe trait
            H::add("ref_unwind_safe", Handler(ITEM_TRAIT, handle_save)); // 1.54 - RefUnwindSafe trait
        }
        {
            H::add("transmute_trait", Handler(ITEM_TRAIT, handle_save)); // 1.74 - `BikeshedIntrinsicFrom` trait
            // - Markers
            H::add("destruct", Handler(ITEM_TRAIT, handle_save));       // 1.74 - `Destruct` trait
            H::add("tuple_trait", Handler(ITEM_TRAIT, handle_save));    // 1.74 - `Tuple` trait (must be implemented for all tuples)
            H::add("pointer_like", Handler(ITEM_TRAIT, handle_save));   // 1.74 - `PointerLike` trait
            H::add("const_param_ty", Handler(ITEM_TRAIT, handle_save)); // 1.74 - `ConstParamTy` trait
            H::add("fn_ptr_trait", Handler(ITEM_TRAIT, handle_save));   // 1.74 - `FnPtr` trait

            // Structs
            H::add("transmute_opts", Handler(ITEM_STRUCT, handle_save)); // 1.74 - `Assume` struct
            H::add("ptr_unique", Handler(ITEM_STRUCT, handle_save));     // 1.74 - `::core::ptr::Unique`
            H::add("CStr", Handler(ITEM_STRUCT, handle_save));           // 1.74 - `::core::ffi::CStr` - Why? (miri?)
            H::add("String", Handler(ITEM_STRUCT, handle_save));         // 1.74 - `::alloc::string::String` - Why? (miri?)

            H::add("from_yeet", Handler(ITEM_FN, handle_save));                            // 1.74 - `::core::try_trait::from_yeet`
            H::add("panic_nounwind", Handler(ITEM_FN, handle_save));                       // 1.74 - `::core::panicking::panic`
            H::add("panic_display", Handler(ITEM_FN, handle_save));                        // 1.74 - `::core::panicking::panic_display`
            H::add("panic_bounds_check", Handler(ITEM_FN, handle_save));                   // 1.74 - `::core::panicking::panic_bounds_check`
            H::add("panic_misaligned_pointer_dereference", Handler(ITEM_FN, handle_save)); // 1.74 - `::core::panicking::panic_misaligned_pointer_dereference`
            H::add("panic_cannot_unwind", Handler(ITEM_FN, handle_save));                  // 1.74 - `::core::panicking::panic_cannot_unwind`
            H::add("panic_in_cleanup", Handler(ITEM_FN, handle_save));                     // 1.74 - `::core::panicking::panic_in_cleanup `
            H::add("const_panic_fmt", Handler(ITEM_FN, handle_save));                      // 1.74 - `::core::panicking::const_panic_fmt`

            // Enums
            H::add("c_void", Handler(ITEM_ENUM, handle_save)); // 1.74 - `::core::ffi::c_void` - Why? (miri?)
            H::add("Option", Handler(ITEM_ENUM, handle_save)); // 1.74 - `::core::option::Option`

            // - Formatting
            H::add("format_arguments", Handler(ITEM_STRUCT, handle_save));   // 1.74 - `::core::fmt::Arguments`
            H::add("format_placeholder", Handler(ITEM_STRUCT, handle_save)); // 1.74 - `::core::fmt::rt::Placeholder`
            H::add("format_argument", Handler(ITEM_STRUCT, handle_save));    // 1.74 - `::core::fmt::rt::Argument`
            H::add("format_unsafe_arg", Handler(ITEM_STRUCT, handle_save));  // 1.74 - `::core::fmt::rt::UnsafeArg`
            H::add("format_alignment", Handler(ITEM_ENUM, handle_save));     // 1.74 - `::core::fmt::rt::Alignment`
            H::add("format_count", Handler(ITEM_ENUM, handle_save));         // 1.74 - `::core::fmt::rt::Count`

            // - Futures
            H::add("ResumeTy", Handler(ITEM_STRUCT, handle_save)); // 1.74 - `::core::future::ResumeTy`
            H::add("Poll", Handler(ITEM_ENUM, handle_save));       // 1.74 - `::core::task::poll::Poll`
            H::add("Context", Handler(ITEM_STRUCT, handle_save));  // 1.74 - `::core::task::wake::Context`
        }
        {
            H::add("contract_build_check_ensures", Handler(ITEM_FN, handle_save)); // 1.90 - `::core::contracts::build_check_ensures`
            H::add("contract_check_requires", Handler(ITEM_FN, handle_save));      // 1.90 - `::core::intrinsics::contract_check_requires`
            H::add("contract_check_ensures", Handler(ITEM_FN, handle_save));       // 1.90 - `::core::intrinsics::contract_check_ensures`
            H::add("use_cloned", Handler(ITEM_TRAIT, handle_save));                // 1.90 - `::core::clone::use_cloned` - for the `.use` syntax

            H::add("Ordering", Handler(ITEM_ENUM, handle_save)); // comparison ordering

            H::add("meta_sized", Handler(ITEM_TRAIT, handle_save));                  // ::core::marker::MetaSized
            H::add("pointee_sized", Handler(ITEM_TRAIT, handle_save));               // ::core::marker::PointeeSized
            H::add("bikeshed_guaranteed_no_drop", Handler(ITEM_TRAIT, handle_save)); // ::core::marker::BikeshedGuaranteedNoDrop
            H::add("unsafe_unpin", Handler(ITEM_TRAIT, handle_save));                // ::core::marker::UnsafeUnpin
            H::add("unsized_const_param_ty", Handler(ITEM_TRAIT, handle_save));      // ::core::marker::UnsizedConstParamTy
            H::add("coerce_pointee_validated", Handler(ITEM_TRAIT, handle_save));    // ::core::marker::CoercePointeeValidated

            H::add("async_fn", Handler(ITEM_TRAIT, handle_save));
            H::add("async_fn_mut", Handler(ITEM_TRAIT, handle_save));
            H::add("async_fn_once", Handler(ITEM_TRAIT, handle_save));

            H::add("async_fn_kind_helper", Handler(ITEM_TRAIT, handle_save)); // ::core::ops::async_function::internal_implementation_detail::AsyncFnKindHelper
            H::add("coroutine_state", Handler(ITEM_ENUM, handle_save));       // ::core::ops::coroutine::CoroutineState
            H::add("coroutine", Handler(ITEM_TRAIT, handle_save));            // ::core::ops::coroutine::Coroutine
            H::add("deref_pure", Handler(ITEM_TRAIT, handle_save));           // ::core::ops::deref::DerefPure
            H::add("legacy_receiver", Handler(ITEM_TRAIT, handle_save));      // ::core::ops::deref::LegacyReceiver

            H::add("type_id", Handler(ITEM_STRUCT, handle_save)); // ::core::any::TypeId

            H::add("async_iterator", Handler(ITEM_TRAIT, handle_save)); // ::core::async_iter::async_iter::AsyncIterator
            H::add("fused_iterator", Handler(ITEM_TRAIT, handle_save)); // ::core::iter::traits::marker::FusedIterator

            // Various panic handlers
            H::add("panic_const_add_overflow", Handler(ITEM_FN, handle_save));
            H::add("panic_const_sub_overflow", Handler(ITEM_FN, handle_save));
            H::add("panic_const_mul_overflow", Handler(ITEM_FN, handle_save));
            H::add("panic_const_div_overflow", Handler(ITEM_FN, handle_save));
            H::add("panic_const_rem_overflow", Handler(ITEM_FN, handle_save));
            H::add("panic_const_neg_overflow", Handler(ITEM_FN, handle_save));
            H::add("panic_const_shr_overflow", Handler(ITEM_FN, handle_save));
            H::add("panic_const_shl_overflow", Handler(ITEM_FN, handle_save));
            H::add("panic_const_div_by_zero", Handler(ITEM_FN, handle_save));
            H::add("panic_const_rem_by_zero", Handler(ITEM_FN, handle_save));
            H::add("panic_const_coroutine_resumed", Handler(ITEM_FN, handle_save));
            H::add("panic_const_async_fn_resumed", Handler(ITEM_FN, handle_save));
            H::add("panic_const_async_gen_fn_resumed", Handler(ITEM_FN, handle_save));
            H::add("panic_const_gen_fn_none", Handler(ITEM_FN, handle_save));
            H::add("panic_const_coroutine_resumed_panic", Handler(ITEM_FN, handle_save));
            H::add("panic_const_async_fn_resumed_panic", Handler(ITEM_FN, handle_save));
            H::add("panic_const_async_gen_fn_resumed_panic", Handler(ITEM_FN, handle_save));
            H::add("panic_const_gen_fn_none_panic", Handler(ITEM_FN, handle_save));

            H::add("panic_const_coroutine_resumed_drop", Handler(ITEM_FN, handle_save));
            H::add("panic_const_async_fn_resumed_drop", Handler(ITEM_FN, handle_save));
            H::add("panic_const_async_gen_fn_resumed_drop", Handler(ITEM_FN, handle_save));
            H::add("panic_const_gen_fn_none_drop", Handler(ITEM_FN, handle_save));

            H::add("panic_null_pointer_dereference", Handler(ITEM_FN, handle_save));
            H::add("panic_invalid_enum_construction", Handler(ITEM_FN, handle_save));

            H::add("unsafe_pinned", Handler(ITEM_STRUCT, handle_save)); // ::core::pin::unsafe_pinned::UnsafePinned

            H::add("RangeCopy", Handler(ITEM_STRUCT, handle_save));          // ::core::range::Range
            H::add("RangeInclusiveCopy", Handler(ITEM_STRUCT, handle_save)); // ::core::range::RangeInclusive
            H::add("RangeFromCopy", Handler(ITEM_STRUCT, handle_save));      // ::core::range::RangeFrom

            H::add("async_drop", Handler(ITEM_TRAIT, handle_save));       // ::core::future::async_drop::AsyncDrop
            H::add("async_drop_in_place", Handler(ITEM_FN, handle_save)); // ::core::future::async_drop::async_drop_in_place

            H::add("global_alloc_ty", Handler(ITEM_STRUCT, handle_save)); // ::alloc::alloc::Global
        }
    }
    const char* real_name = nullptr; // For when lang items have their name changed
    auto it = g_handlers.find(name.c_str());
    if (it != g_handlers.end()) {
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
        real_name = "try";
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
        item.as_Function().markings.link_name = "rust_begin_unwind";
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

    auto rv = crate.mLangItems.insert(::std::make_pair(real_name == nullptr ? name : real_name, path));
    if (!rv.second) {
        const auto& other_path = rv.first->second;
        if (path != other_path) {
            // HACK: Anon modules get visited twice, so can lead to duplicate annotations
            ERROR(sp, E0000, "Duplicate definition of language item '" << name << "' - " << other_path << " and " << path);
        }
    }
}

class DecoratorLangItem: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const AST::Attribute& attr, AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        auto name = attr.parse_equals_string(crate, mod);
        TU_MATCH_HDRA( (i), {)
        default:
            TODO(sp, "Unknown item type " << i.tag_str() << " with #["<<attr<<"] attached at " << path);
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
                if (e.code().is_valid()) {
                    handle_lang_item(sp, crate, path, name, ITEM_FN, i);
                } else {
                    handle_lang_item(sp, crate, path, name, ITEM_EXTERN_FN, i);
                }
            }
            TU_ARMA(Type, e) {
                handle_lang_item(sp, crate, path, name, ITEM_TYPE_ALIAS, i);
            }
            TU_ARMA(Static, e) {
                handle_lang_item(sp, crate, path, name, ITEM_STATIC, i);
            }
            TU_ARMA(Struct, e) {
                handle_lang_item(sp, crate, path, name, ITEM_STRUCT, i);
            }
            TU_ARMA(Enum, e) {
                handle_lang_item(sp, crate, path, name, ITEM_ENUM, i);
            }
            TU_ARMA(Union, e) {
                handle_lang_item(sp, crate, path, name, ITEM_UNION, i);
            }
            TU_ARMA(Trait, e) {
                handle_lang_item(sp, crate, path, name, ITEM_TRAIT, i);
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
                const auto& other_path = rv.first->second;
                ERROR(sp, E0000, "Duplicate definition of #[main] - " << other_path << " and " << path);
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
                const auto& other_path = rv.first->second;
                ERROR(sp, E0000, "Duplicate definition of #[start] - " << other_path << " and " << path);
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
                const auto& other_path = rv.first->second;
                ERROR(sp, E0000, "Duplicate definition of #[panic_implementation] - " << other_path << " and " << path);
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
                const auto& other_path = rv.first->second;
                ERROR(sp, E0000, "Duplicate definition of #[panic_handler] - " << other_path << " and " << path);
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
                const auto& other_path = rv.first->second;
                ERROR(sp, E0000, "Duplicate definition of #[alloc_error_handler] - " << other_path << " and " << path);
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
        expr->set_span(span);
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
            ERROR(sp, E0000, "#[test] can only be put on functions - found on " << i.tag_str());
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
            ERROR(sp, E0000, "#[should_panic] can only be put on functions - found on " << i.tag_str());
        }

        if (crate.testHarness) {
            // TODO: If this test doesn't yet exist, create it (but as disabled)?
            for (auto& td : crate.tests) {
                if (td.path != path) {
                    continue;
                }

                if (mi.data().size() != 0) {
                    td.panic_type = ::AST::TestDesc::ShouldPanic::YesWithMessage;

                    TTStream lex(sp, ParseState(), mi.data());
                    auto parse_message = [&]() {
                        auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
                        if (auto* v = cast<::AST::ExprNodeString>(&*n)) {
                            td.expected_panic_message = v->mValue;
                        } else {
                            throw ParseError::Unexpected(lex, Token(InterpolatedFragment(InterpolatedFragment::EXPR, n.release())), TOK_STRING);
                        }
                    };
                    if (lex.getTokenIf(TOK_EQUAL)) {
                        parse_message();
                    } else {
                        lex.getTokenCheck(TOK_PAREN_OPEN);
                        while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
                            auto n = lex.getTokenCheck(TOK_IDENT).ident().name;
                            if (n == "expected") {
                                lex.getTokenCheck(TOK_EQUAL);
                                parse_message();
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
                    td.panic_type = ::AST::TestDesc::ShouldPanic::Yes;
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
            ERROR(sp, E0000, "#[ignore] can only be put on functions - found on " << i.tag_str());
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
