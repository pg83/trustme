//@ crate-type: lib
//@ compile-fail: pattern
#![feature(pattern_types, pattern_type_macro)]

pub type Tagged = std::pat::pattern_type!(String is !null);

#[no_mangle]
pub fn duplicate(value: &Tagged) -> Tagged {
    value.clone()
}
