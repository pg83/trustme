//@ compile-fail: Unexpected token
#![feature(pattern_types, pattern_type_macro)]
fn main() { let _ = std::pat::pattern_type!(u16 is 5..=13); }
