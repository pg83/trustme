// Extracted from library/core/src/cell.rs:884
#![allow(unused)]
fn main() {
    use std::cell::RefCell;
    let cell = RefCell::new(5);
    let old_value = cell.replace(6);
    assert_eq!(old_value, 5);
    assert_eq!(cell, RefCell::new(6));
}
