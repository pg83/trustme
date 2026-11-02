//@ compile-fail: No macro arm matched
#![feature(builtin_syntax)]
macro_rules! evaluate { ($value:expr) => { $value }; }
fn main() { let _ = evaluate!(builtin # (19)); }
