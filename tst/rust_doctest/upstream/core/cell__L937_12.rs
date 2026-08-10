// Extracted from library/core/src/cell.rs:937
#![allow(unused)]
fn main() {
    use std::cell::RefCell;
    let c = RefCell::new(5);
    let d = RefCell::new(6);
    c.swap(&d);
    assert_eq!(c, RefCell::new(6));
    assert_eq!(d, RefCell::new(5));
}
