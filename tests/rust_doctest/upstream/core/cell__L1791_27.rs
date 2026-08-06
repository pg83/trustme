// Extracted from library/core/src/cell.rs:1791
#![allow(unused)]
#![feature(cell_leak)]
fn main() {
    use std::cell::{RefCell, RefMut};
    let cell = RefCell::new(0);
    
    let value = RefMut::leak(cell.borrow_mut());
    assert_eq!(*value, 0);
    *value = 1;
    
    assert!(cell.try_borrow_mut().is_err());
}
