//@ compile-fail: on non-macro
#![feature(rustc_attrs)]

#[rustc_builtin_macro]
struct NotAMacro;

fn main() {}
