// Extracted from library/core/src/cell.rs:513
#![allow(unused)]
fn main() {
    use std::cell::Cell;

    let c = Cell::new(5);
    let five = c.into_inner();

    assert_eq!(five, 5);
}
