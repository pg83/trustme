// Extracted from library/core/src/cell.rs:1624
#![allow(unused)]
#![feature(cell_leak)]
fn main() {
    use std::cell::{RefCell, Ref};
    let cell = RefCell::new(0);
    
    let value = Ref::leak(cell.borrow());
    assert_eq!(*value, 0);
    
    assert!(cell.try_borrow().is_ok());
    assert!(cell.try_borrow_mut().is_err());
}
