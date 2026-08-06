// Extracted from library/alloc/src/rc.rs:1147
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    
    use std::rc::Rc;
    use std::alloc::System;
    
    let values = Rc::<[u32], _>::new_zeroed_slice_in(3, System);
    let values = unsafe { values.assume_init() };
    
    assert_eq!(*values, [0, 0, 0])
}
