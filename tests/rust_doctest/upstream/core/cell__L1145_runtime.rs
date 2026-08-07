// Extracted from library/core/src/cell.rs:1145
#![allow(unused)]
fn main() {
    use std::cell::RefCell;

    let c = RefCell::new(5);

    let ptr = c.as_ptr();
}
