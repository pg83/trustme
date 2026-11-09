//@ compile-fail: on macro other than macro_rules
#![feature(rustc_attrs)]

#[rustc_builtin_macro]
stringify!(not_a_definition);

fn main() {}
