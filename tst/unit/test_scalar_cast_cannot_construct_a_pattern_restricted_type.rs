#![feature(pattern_types, pattern_type_macro)]
//@ compile-fail: Invalid cast [dst]

type Small = std::pat::pattern_type!(u16 is 1..=9);

fn cast(value: u16) -> Small {
    value as Small
}

fn main() {}
