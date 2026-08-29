#include "ast_attrs.h"

#include "output.h"
#include "synext.h"
#include "ast_expr.h"
#include "ast_crate.h"
#include "parse_common.h"
#include "expand_common.h"
#include "parse_ttstream.h"
#include "parse_parseerror.h"
#include "parse_interpolated_fragment.h"

using namespace stl;

namespace {
    std::vector<ASTAttribute> cloneMivec(const std::vector<ASTAttribute>& values) {
        std::vector<ASTAttribute> result;
        result.reserve(values.size());
        for (const auto& value : values) {
            result.push_back(value.clone());
        }
        return result;
    }
}

ASTAttributeList::ASTAttributeList() = default;

ASTAttributeList::ASTAttributeList(std::vector<ASTAttribute> items)
    : items(mv$(items))
{
}

ASTAttributeList::~ASTAttributeList() = default;
ASTAttributeList::ASTAttributeList(ASTAttributeList&&) = default;
ASTAttributeList& ASTAttributeList::operator=(ASTAttributeList&&) = default;
ASTAttributeList::ASTAttributeList(const ASTAttributeList&) = default;

ASTAttributeList ASTAttributeList::clone() const {
    return ASTAttributeList(cloneMivec(items));
}

void ASTAttributeList::push_back(ASTAttribute item) {
    items.push_back(std::move(item));
}

const ASTAttribute* ASTAttributeList::get(const char* name) const {
    for (const auto& item : items) {
        if (item.name() == name) {
            return &item;
        }
    }
    return nullptr;
}

ASTAttribute::ASTAttribute(Span sp, ASTAttributeName name, TokenTree data)
    : span_(std::move(sp))
    , name_(std::move(name))
    , data_(std::move(data))
    , isInert_(false)
{
}

ASTAttribute::ASTAttribute(const ASTAttribute& value)
    : span_(value.span_)
    , name_(value.name_)
    , data_(value.data_.clone())
    , isInert_(value.isInert_)
{
}

ASTAttribute ASTAttribute::clone() const {
    return ASTAttribute(*this);
}

void ASTAttribute::fmt(ZeroCopyOutput& out) const {
    out << name_ << data_;
}

std::string ASTAttribute::parseEqualsString(const WireBoard& wb, const ASTCrate& crate, const ASTModule& mod) const {
    TTStream lex(span_, ParseState(), data());
    lex.parseState().wb = &wb;
    lex.getTokenCheck(TOK_EQUAL);
    auto node = ExpandParseAndExpandExprVal(crate, mod, lex);

    std::string result;
    if (auto* value = cast<ASTExprNodeString>(&*node)) {
        result = value->value;
    } else {
        parseErrorUnexpected(lex, Token(InterpolatedFragment(InterpolatedFragment::EXPR, node.release())), TOK_STRING);
    }
    lex.getTokenCheck(TOK_EOF);
    return result;
}

std::string ASTAttribute::parseParenString() const {
    TTStream lex(span_, ParseState(), data());
    lex.getTokenCheck(TOK_PAREN_OPEN);
    auto result = lex.getTokenCheck(TOK_STRING).str();
    lex.getTokenCheck(TOK_PAREN_CLOSE);
    return result;
}

void ASTAttribute::parseParenIdentListCb(ASTAttributeIdentCallback& itemCb) const {
    TTStream lex(span_, ParseState(), data());
    lex.getTokenCheck(TOK_PAREN_OPEN);
    while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
        itemCb.visit(lex.pointSpan(), lex.getTokenCheck(TOK_IDENT).ident().name);
        if (lex.lookahead(0) != TOK_COMMA) {
            break;
        }
        lex.getTokenCheck(TOK_COMMA);
    }
    lex.getTokenCheck(TOK_PAREN_CLOSE);
}

namespace stl {
    template <>
    void output<ZeroCopyOutput, ASTAttribute>(ZeroCopyOutput& os, const ASTAttribute& x) {
        x.fmt(os);
        return;
    }

    template <>
    void output<ZeroCopyOutput, ASTAttributeList>(ZeroCopyOutput& out, const ASTAttributeList& value) {
        for (const auto& item : value.items) {
            out << StringView("#[") << item << StringView("]");
        }
    }

    template <>
    void output<ZeroCopyOutput, ASTAttributeName>(ZeroCopyOutput& out, const ASTAttributeName& value) {
        if (value.elems.empty()) {
            out << StringView("<empty>");
            return;
        }
        for (const auto& item : value.elems) {
            if (&item != &value.elems.front()) {
                out << StringView("::");
            }
            out << item;
        }
    }
}
