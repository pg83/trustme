// Extracted from library/alloc/src/vec/mod.rs:570
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::ptr;
    use std::mem;
    
    let v = vec![1, 2, 3];
    
    
    // Prevent running `v`'s destructor so we are in complete control
    // of the allocation.
    let mut v = mem::ManuallyDrop::new(v);
    
    // Pull out the various important pieces of information about `v`
    let p = v.as_mut_ptr();
    let len = v.len();
    let cap = v.capacity();
    
    unsafe {
        // Overwrite memory with 4, 5, 6
        for i in 0..len {
            ptr::write(p.add(i), 4 + i);
        }
    
        // Put everything back together into a Vec
        let rebuilt = Vec::from_raw_parts(p, len, cap);
        assert_eq!(rebuilt, [4, 5, 6]);
    }
}
