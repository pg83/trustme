//@ compile-fail: pattern types require a range pattern
#![feature(pattern_types, pattern_type_macro)]

type Single = std::pat::pattern_type!(u16 is 19);
fn main() { let _: Single = 19; }
