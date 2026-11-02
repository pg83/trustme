//@ compile-fail: Unexpected token
#![feature(pattern_types, pattern_type_macro)]

fn main() {
    let _ = concat!(std::pat::pattern_type!(u16 is 2..=7));
}
