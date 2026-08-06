// Extracted from library/alloc/src/rc.rs:1596
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    
    let five = Rc::new(5);
    let _weak_five = Rc::downgrade(&five);
    
    assert_eq!(1, Rc::weak_count(&five));
}
