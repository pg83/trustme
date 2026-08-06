// Extracted from library/core/src/cell.rs:1209
#![allow(unused)]
#![feature(cell_leak)]
fn main() {
    use std::cell::RefCell;
    
    let mut c = RefCell::new(0);
    std::mem::forget(c.borrow_mut());
    
    assert!(c.try_borrow().is_err());
    c.undo_leak();
    assert!(c.try_borrow().is_ok());
}
