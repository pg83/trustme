//@ compile-variant: -O
use std::cell::Cell;
fn ordinary(value: &Cell<u16>) -> &Cell<u16> { &*value }
const fn constant_function(value: &Cell<u16>) -> &Cell<u16> { &*value }
fn main() {
    let value = Cell::new(17);
    ordinary(&value).set(23);
    constant_function(&value).set(29);
    assert_eq!(value.get(), 29);
}
