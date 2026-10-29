//@ compile-fail: pattern not supported in pattern types
#![feature(pattern_types, pattern_type_macro)]
use std::pat::pattern_type;
type Invalid = pattern_type!(u32 is binding);
fn main() {}
