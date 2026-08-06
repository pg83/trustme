// Extracted from library/alloc/src/rc.rs:2328
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    
    let five = Rc::new(5);
    
    let _ = Rc::clone(&five);
}
