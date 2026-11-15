//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: Unexpected token TOK_IDENT"wrong"

#![feature(rustc_attrs)]

#[rustc_builtin_macro]
macro_rules! pattern_type {
    ($($tokens:tt)*) => {};
}

type Invalid = pattern_type!(u8 is !wrong);
