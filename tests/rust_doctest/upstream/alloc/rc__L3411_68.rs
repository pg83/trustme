// Extracted from library/alloc/src/rc.rs:3411
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    
    let first_rc = Rc::new(5);
    let first = Rc::downgrade(&first_rc);
    let second = Rc::downgrade(&first_rc);
    
    assert!(first.ptr_eq(&second));
    
    let third_rc = Rc::new(5);
    let third = Rc::downgrade(&third_rc);
    
    assert!(!first.ptr_eq(&third));
}
