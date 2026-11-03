//@ compile-fail: is
#![feature(pattern_types, pattern_type_macro)]

fn main() {
    let _ = std::pat::pattern_type!(u16 is 3..=11);
}
