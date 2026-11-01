//@ compile-fail: expected
#![feature(macro_metavar_expr)]
macro_rules! bad { () => { ${count(17)} }; }
fn main() { let _ = bad!(); }
