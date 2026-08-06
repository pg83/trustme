// Extracted from library/alloc/src/boxed.rs:803
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    
    use std::alloc::System;
    
    let values = Box::<[u32], _>::new_zeroed_slice_in(3, System);
    let values = unsafe { values.assume_init() };
    
    assert_eq!(*values, [0, 0, 0])
}
