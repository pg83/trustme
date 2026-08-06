// Extracted from library/alloc/src/rc.rs:547
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    
    let five = Rc::try_new(5);
    Ok::<(), std::alloc::AllocError>(())
}
