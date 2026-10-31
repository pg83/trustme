//@ compile-fail: string pattern types
#![feature(pattern_types, pattern_type_macro)]
type Range = std::pat::pattern_type!(str is "a"..="z");
fn main() {}
