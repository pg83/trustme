//@ compile-fail: expected
#![feature(macro_metavar_expr)]
macro_rules! bad { () => {{ ${ignore(17)} 7u16 }}; }
fn main() { let _ = bad!(); }
