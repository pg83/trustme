#pragma once

#include "parse_tokentree.h"
#include "span.h"
#include "ast_attrs.h"
#include "ast_path.h"


    class ASTMacroInvocation {
        Span mSpan;

        ASTPath macroPath;
        RcString ident;
        TokenTree input;
        bool mIsExpanded = false;

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
            return mSpan;
        }

        const ASTPath& path() const {
            return macroPath;
        }

        bool isExpanded() const {
            return mIsExpanded;
        }

        void setExpanded() {
            mIsExpanded = true;
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

        friend ::std::ostream& operator<<(::std::ostream& os, const ASTMacroInvocation& x);
    };

