// Extracted from library/alloc/src/vec/mod.rs:998
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    
    use std::alloc::System;
    
    use std::ptr;
    use std::mem;
    
    let mut v = Vec::with_capacity_in(3, System);
    v.push(1);
    v.push(2);
    v.push(3);
    
    
    // Prevent running `v`'s destructor so we are in complete control
    // of the allocation.
    let mut v = mem::ManuallyDrop::new(v);
    
    // Pull out the various important pieces of information about `v`
    let p = v.as_mut_ptr();
    let len = v.len();
    let cap = v.capacity();
    let alloc = v.allocator();
    
    unsafe {
        // Overwrite memory with 4, 5, 6
        for i in 0..len {
            ptr::write(p.add(i), 4 + i);
        }
    
        // Put everything back together into a Vec
        let rebuilt = Vec::from_raw_parts_in(p, len, cap, alloc.clone());
        assert_eq!(rebuilt, [4, 5, 6]);
    }
}
