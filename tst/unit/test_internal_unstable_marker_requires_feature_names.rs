#![feature(decl_macro, allow_internal_unstable)]
//@ compile-fail: expected TOK_IDENT

#[allow_internal_unstable(17)]
macro bad() {
    7u16
}

fn main() {
    assert_eq!(bad!(), 7);
}
