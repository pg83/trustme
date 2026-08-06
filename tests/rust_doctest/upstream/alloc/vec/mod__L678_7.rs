// Extracted from library/alloc/src/vec/mod.rs:678
#![allow(unused)]
#![feature(box_vec_non_null)]
extern crate alloc;
fn main() {
    
    use std::ptr::NonNull;
    use std::mem;
    
    let v = vec![1, 2, 3];
    
    
    // Prevent running `v`'s destructor so we are in complete control
    // of the allocation.
    let mut v = mem::ManuallyDrop::new(v);
    
    // Pull out the various important pieces of information about `v`
    let p = unsafe { NonNull::new_unchecked(v.as_mut_ptr()) };
    let len = v.len();
    let cap = v.capacity();
    
    unsafe {
        // Overwrite memory with 4, 5, 6
        for i in 0..len {
            p.add(i).write(4 + i);
        }
    
        // Put everything back together into a Vec
        let rebuilt = Vec::from_parts(p, len, cap);
        assert_eq!(rebuilt, [4, 5, 6]);
    }
}
