//@ aux-build: pattern_type_exports.rs
#![feature(pattern_types, pattern_type_macro)]
use std::pat::pattern_type;
fn main() {
    let bounded: pattern_type!(u32 is 2..=6) = 5;
    let below: pattern_type_exports::Below = 3;
    let above: pattern_type_exports::Above = 11;
    assert_eq!(pattern_type_exports::bounded(bounded), 5);
    assert_eq!(pattern_type_exports::below(below), 3);
    assert_eq!(pattern_type_exports::above(above), 11);
}
