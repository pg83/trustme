//@ compile-fail: Unexpected token
#![feature(pattern_types, pattern_type_macro)]
fn main() { let _ = std::pat::pattern_type!(u8 is 2..=7); }
