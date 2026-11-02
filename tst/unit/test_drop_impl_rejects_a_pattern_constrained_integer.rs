//@ compile-fail: Drop can only be implemented for structs, enums, and unions
#![feature(pattern_types, pattern_type_macro)]
type Small = std::pat::pattern_type!(u8 is 1..=7);
impl Drop for Small { fn drop(&mut self) {} }
fn main() {}
