//@ compile-variant: -O
use std::cell::Cell;
const OPTION: &Option<Cell<u16>> = &None;
const ARRAY: &[Cell<u16>; 0] = &[];
fn main() {
    assert!(OPTION.is_none());
    assert!(ARRAY.is_empty());
}
