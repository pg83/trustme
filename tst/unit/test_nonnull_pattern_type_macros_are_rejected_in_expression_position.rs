//@ compile-fail: Unexpected token
#![feature(pattern_types, pattern_type_macro)]
fn main() { let _ = std::pat::pattern_type!(*const u8 is !null); }
