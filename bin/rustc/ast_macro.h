#pragma once

#include "parse_tokentree.h"
#include "span.h"
#include "ast_attrs.h"
#include "ast_path.h"

namespace AST {

    class MacroInvocation {
        Span mSpan;

        AST::Path macroPath;
        RcString ident;
        TokenTree input;
        bool isExpanded = false;

    public:
        MacroInvocation(MacroInvocation&&) = default;
        MacroInvocation& operator=(MacroInvocation&&) = default;
        MacroInvocation(const MacroInvocation&) = delete;
        MacroInvocation& operator=(const MacroInvocation&) = delete;

        MacroInvocation();

        MacroInvocation(Span span, AST::Path macro, RcString ident, TokenTree input);

        MacroInvocation clone() const;

        void clear();

        const Span& span() const {
            return mSpan;
        }

        const AST::Path& path() const {
            return macroPath;
        }

        bool is_expanded() const {
            return isExpanded;
        }

        void setExpanded() {
            isExpanded = true;
        }

        const RcString& inputIdent() const {
            return ident;
        }

        const TokenTree& input_tt() const {
            return input;
        }

        TokenTree& input_tt() {
            return input;
        }

        friend ::std::ostream& operator<<(::std::ostream& os, const MacroInvocation& x);
    };

}
