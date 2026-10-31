//@ compile-fail: pattern not supported in pattern types
#![feature(never_type, pattern_types, pattern_type_macro)]
use std::pat::pattern_type;
type Never = pattern_type!(! is ..);
fn classify(value: &Never) -> u8 { match value { _ => 17 } }
fn main() { let _function: fn(&Never) -> u8 = classify; }
