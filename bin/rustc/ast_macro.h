#pragma once

#include "span.h"
#include "ast_path.h"
#include "ast_attrs.h"
#include "parse_tokentree.h"

class ASTMacroInvocation {
    Span span_;

    ASTPath macroPath;
    RcString ident;
    TokenTree input;
    bool isExpanded_ = false;

public:
    ASTMacroInvocation(ASTMacroInvocation&&) = default;
    ASTMacroInvocation& operator=(ASTMacroInvocation&&) = default;
    ASTMacroInvocation(const ASTMacroInvocation&) = delete;
    ASTMacroInvocation& operator=(const ASTMacroInvocation&) = delete;

    ASTMacroInvocation();

    ASTMacroInvocation(Span span, ASTPath macro, RcString ident, TokenTree input);

    ASTMacroInvocation clone() const;

    void clear();

    const Span& span() const {
        return span_;
    }

    const ASTPath& path() const {
        return macroPath;
    }

    bool isExpanded() const {
        return isExpanded_;
    }

    void setExpanded() {
        isExpanded_ = true;
    }

    const RcString& inputIdent() const {
        return ident;
    }

    const TokenTree& inputTt() const {
        return input;
    }

    TokenTree& inputTt() {
        return input;
    }

};
