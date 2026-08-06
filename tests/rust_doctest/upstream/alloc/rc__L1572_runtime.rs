// Extracted from library/alloc/src/rc.rs:1572
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    
    let five = Rc::new(5);
    
    let weak_five = Rc::downgrade(&five);
}
