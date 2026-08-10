// Extracted from library/core/src/cell.rs:574
#![allow(unused)]
fn main() {
    use std::cell::Cell;

    let c = Cell::new(5);

    let ptr = c.as_ptr();
}
