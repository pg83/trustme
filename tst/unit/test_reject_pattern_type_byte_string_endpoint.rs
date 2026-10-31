//@ compile-fail: byte-string pattern types
#![feature(pattern_types, pattern_type_macro)]
type Range = std::pat::pattern_type!([u8; 1] is b"a"..=b"z");
fn main() {}
