//@ compile-fail: metavariable
#![feature(macro_metavar_expr)]
macro_rules! bad { ($name:ident) => { ${mystery($name)} }; }
fn main() { let _ = bad!(known); }
