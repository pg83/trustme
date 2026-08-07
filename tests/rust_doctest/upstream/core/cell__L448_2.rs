// Extracted from library/core/src/cell.rs:448
#![allow(unused)]
fn main() {
    use std::cell::Cell;

    let c1 = Cell::new(5i32);
    let c2 = Cell::new(10i32);
    c1.swap(&c2);
    assert_eq!(10, c1.get());
    assert_eq!(5, c2.get());
}
