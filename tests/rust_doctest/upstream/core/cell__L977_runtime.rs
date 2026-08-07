// Extracted from library/core/src/cell.rs:977
#![allow(unused)]
fn main() {
    use std::cell::RefCell;

    let c = RefCell::new(5);

    let m = c.borrow_mut();
    let b = c.borrow(); // this causes a panic
}
