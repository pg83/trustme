// Extracted from library/alloc/src/rc.rs:2542
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    
    let five = Rc::new(5);
    
    assert!(five <= Rc::new(5));
}
