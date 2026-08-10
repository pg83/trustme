// Extracted from library/core/src/cell.rs:491
#![allow(unused)]
fn main() {
    use std::cell::Cell;

    let cell = Cell::new(5);
    assert_eq!(cell.get(), 5);
    assert_eq!(cell.replace(10), 5);
    assert_eq!(cell.get(), 10);
}
