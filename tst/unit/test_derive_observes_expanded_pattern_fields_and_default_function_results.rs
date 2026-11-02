//@ proc-macro-aux-build: observe_expanded_pattern_fields.rs
//@ compile-variant: -O
#![feature(pattern_types, pattern_type_macro)]

#[derive(observe_expanded_pattern_fields::ObserveFields)]
struct Shape {
    small: std::pat::pattern_type!(u8 is 3..=9),
    callback: fn(),
}

fn main() {
    assert_eq!(Shape::OBSERVED, core::mem::size_of::<Shape>());
    assert!(Shape::OBSERVED >= core::mem::size_of::<fn()>() + 1);
}
