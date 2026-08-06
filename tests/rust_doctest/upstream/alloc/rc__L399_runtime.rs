// Extracted from library/alloc/src/rc.rs:399
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    
    let five = Rc::new(5);
}
