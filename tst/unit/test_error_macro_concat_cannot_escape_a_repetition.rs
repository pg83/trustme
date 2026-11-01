//@ compile-fail: repeating
#![feature(macro_metavar_expr_concat)]
macro_rules! bad { ($($name:ident),*) => { fn ${concat(read_, $name)}() {} }; }
bad!(alpha, beta);
fn main() {}
