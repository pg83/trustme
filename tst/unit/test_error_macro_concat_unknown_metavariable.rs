//@ compile-fail: ghost
#![feature(macro_metavar_expr_concat)]
macro_rules! bad { ($name:ident) => { fn ${concat($ghost, tail)}() {} }; }
bad!(known);
fn main() {}
