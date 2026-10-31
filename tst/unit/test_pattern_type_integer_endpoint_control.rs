#![feature(pattern_types, pattern_type_macro)]
type Range = std::pat::pattern_type!(u8 is 1..=3);
fn main() { assert_eq!(core::mem::size_of::<Range>(), 1); }
