/*
* MRustC - Rust Compiler
* - By John Hodge (Mutabah/thePowersGang)
*
* expand/compile_error.cpp
* - compile_error! handler
*/
#include "synext.hpp"
#include "parse_common.hpp"
#include "parse_parseerror.hpp"
#include "parse_tokentree.hpp"
#include "parse_ttstream.hpp"
#include "parse_lex.hpp" // For Codepoint
#include "ast_expr.hpp"
#include "ast_crate.hpp"

class CExpander_CompileError:
    public ExpandProcMacro
{
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override
    {
        ERROR(sp, E0000, "compile_error! " << tt);
    }
};

STATIC_MACRO("compile_error", CExpander_CompileError);

