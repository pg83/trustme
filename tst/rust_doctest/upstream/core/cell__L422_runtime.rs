// Extracted from library/core/src/cell.rs:422
#![allow(unused)]
fn main() {
    use std::cell::Cell;

    let c = Cell::new(5);

    c.set(10);
}
