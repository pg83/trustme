// Extracted from library/alloc/src/vec/mod.rs:897
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    
    use std::alloc::System;
    
    let mut vec = Vec::with_capacity_in(10, System);
    
    // The vector contains no items, even though it has capacity for more
    assert_eq!(vec.len(), 0);
    assert!(vec.capacity() >= 10);
    
    // These are all done without reallocating...
    for i in 0..10 {
        vec.push(i);
    }
    assert_eq!(vec.len(), 10);
    assert!(vec.capacity() >= 10);
    
    // ...but this may make the vector reallocate
    vec.push(11);
    assert_eq!(vec.len(), 11);
    assert!(vec.capacity() >= 11);
    
    // A vector of a zero-sized type will always over-allocate, since no
    // allocation is necessary
    let vec_units = Vec::<(), System>::with_capacity_in(10, System);
    assert_eq!(vec_units.capacity(), usize::MAX);
}
