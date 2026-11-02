//@ compile-fail: A constant may not refer to interior mutable data
use std::cell::Cell;
const VALUE: &(u16, Cell<u16>) = &(17, Cell::new(23));
fn main() { assert_eq!(VALUE.1.get(), 23); }
