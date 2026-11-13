//@ proc-macro-aux-build: pattern_typed_field_derive.rs
#![feature(pattern_types, pattern_type_macro)]

use pattern_typed_field_derive::KeepField;

macro_rules! define {
    ($field:ty) => {
        #[derive(Clone, KeepField)]
        struct Bounded($field);
    };
}

define!(std::pat::pattern_type!(u8 is 2..=8));

fn main() {
    let value = Bounded(unsafe { std::mem::transmute(5u8) });
    let field = value.clone().into_field();
    let byte: u8 = unsafe { std::mem::transmute(field) };
    assert_eq!(byte, 5);
}
