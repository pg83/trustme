//@ compile-fail: concat with non-ident
#![feature(macro_metavar_expr_concat)]
macro_rules! bad { ($x:tt) => { fn ${concat(prefix, $x)}() {} } }
bad!(1);
fn main() {}
