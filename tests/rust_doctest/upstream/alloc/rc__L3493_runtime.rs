// Extracted from library/alloc/src/rc.rs:3493
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::{Rc, Weak};
    
    let weak_five = Rc::downgrade(&Rc::new(5));
    
    let _ = Weak::clone(&weak_five);
}
