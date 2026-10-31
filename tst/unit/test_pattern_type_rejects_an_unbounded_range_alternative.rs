//@ compile-fail: pattern not supported in pattern types
#![feature(pattern_types, pattern_type_macro)]
use std::pat::pattern_type;
type Entire = pattern_type!(u32 is 1..=3 | ..);
fn main() { let _value: Entire = 37; }
