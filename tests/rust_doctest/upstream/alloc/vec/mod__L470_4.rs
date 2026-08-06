// Extracted from library/alloc/src/vec/mod.rs:470
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = Vec::with_capacity(10);
    
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
    let vec_units = Vec::<()>::with_capacity(10);
    assert_eq!(vec_units.capacity(), usize::MAX);
}
