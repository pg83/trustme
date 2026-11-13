//@ proc-macro-aux-build: pattern_typed_field_derive.rs

use pattern_typed_field_derive::KeepField;

#[derive(KeepField)]
struct Bounded(for<'a> fn(&'a u8) -> u16);

fn widen(value: &u8) -> u16 { u16::from(*value) + 5 }

fn main() {
    static SAVED: u8 = 11;
    let local = 7;
    let field = Bounded(widen).into_field();
    assert_eq!(field(&local), 12);
    assert_eq!(field(&SAVED), 16);
}
