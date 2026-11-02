//@ aux-build: unit_value_and_constructor_exports.rs
//@ compile-variant: -O
use unit_value_and_constructor_exports::{Unit, UNIT, EXPOSED, MAKE};
fn main() {
    let Unit = UNIT;
    assert_eq!(std::mem::size_of_val(&EXPOSED), 0);
    assert_eq!(MAKE(47).0, 47);
}
