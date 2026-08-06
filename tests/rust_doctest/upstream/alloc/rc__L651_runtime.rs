// Extracted from library/alloc/src/rc.rs:651
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    use std::alloc::System;
    
    let five = Rc::new_in(5, System);
}
