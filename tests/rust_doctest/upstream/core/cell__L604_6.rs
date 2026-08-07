// Extracted from library/core/src/cell.rs:604
#![allow(unused)]
fn main() {
    use std::cell::Cell;

    let mut c = Cell::new(5);
    *c.get_mut() += 1;

    assert_eq!(c.get(), 6);
}
