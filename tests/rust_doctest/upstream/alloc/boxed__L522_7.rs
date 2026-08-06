// Extracted from library/alloc/src/boxed.rs:522
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    
    use std::alloc::System;
    
    let zero = Box::<u32, _>::new_zeroed_in(System);
    let zero = unsafe { zero.assume_init() };
    
    assert_eq!(*zero, 0)
}
