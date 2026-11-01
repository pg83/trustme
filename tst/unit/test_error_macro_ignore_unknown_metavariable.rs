//@ compile-fail: ghost
#![feature(macro_metavar_expr)]
macro_rules! bad { ($name:ident) => {{ ${ignore($ghost)} 7u16 }}; }
fn main() { let _ = bad!(known); }
