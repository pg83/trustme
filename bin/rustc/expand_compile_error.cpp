/*
* MRustC - Rust Compiler
* - By John Hodge (Mutabah/thePowersGang)
*
* expand/compile_error.cpp
* - compile_error! handler
*/
#include "synext.h"
#include "parse_common.h"
#include "parse_parseerror.h"
#include "parse_tokentree.h"
#include "parse_ttstream.h"
#include "parse_lex.h" // For Codepoint
#include "ast_expr.h"
#include "ast_crate.h"

class CExpander_CompileError: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        ERROR(sp, E0000, "compile_error! " << tt);
    }
};

STATIC_MACRO("compile_error", CExpander_CompileError);
