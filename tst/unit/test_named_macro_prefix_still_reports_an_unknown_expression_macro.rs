//@ compile-fail: Unknown macro
macro_rules! keep { ($value:expr) => { $value }; }
fn main() { let _ = keep!(missing_expression_macro! named (19)); }
