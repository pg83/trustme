// Extracted from library/alloc/src/boxed.rs:452
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    
    use std::alloc::System;
    
    let mut five = Box::<u32, _>::new_uninit_in(System);
    // Deferred initialization:
    five.write(5);
    let five = unsafe { five.assume_init() };
    
    assert_eq!(*five, 5)
}
