//@ compile-fail: Unexpected token TOK_EOF
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(decl_macro)]

macro unfinished {
    (
