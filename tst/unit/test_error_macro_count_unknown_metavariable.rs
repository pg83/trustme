//@ compile-fail: ghost
#![feature(macro_metavar_expr)]
macro_rules! bad { ($($name:ident),*) => { ${count($ghost)} }; }
fn main() { let _ = bad!(known); }
