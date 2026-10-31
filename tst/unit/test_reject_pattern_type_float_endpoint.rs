//@ compile-fail: float pattern types
#![feature(pattern_types, pattern_type_macro)]
type Range = std::pat::pattern_type!(f32 is 1.0..=2.0);
fn main() {}
