// Extracted from library/core/src/cell.rs:1075
#![allow(unused)]
fn main() {
    use std::cell::RefCell;
    
    let c = RefCell::new(5);
    let m = c.borrow();
    
    let b = c.borrow_mut(); // this causes a panic
}
