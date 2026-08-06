// Extracted from library/alloc/src/rc.rs:3428
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::{Rc, Weak};
    
    let first = Weak::new();
    let second = Weak::new();
    assert!(first.ptr_eq(&second));
    
    let third_rc = Rc::new(());
    let third = Rc::downgrade(&third_rc);
    assert!(!first.ptr_eq(&third));
}
