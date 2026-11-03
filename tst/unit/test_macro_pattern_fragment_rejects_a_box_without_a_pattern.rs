//@ compile-fail: macro
#![feature(box_patterns)]
macro_rules! select { ($pattern:pat => $value:expr) => { $value }; }
fn main() { let _ = select!(box => 7); }
