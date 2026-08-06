// Extracted from library/alloc/src/vec/mod.rs:1844
#![allow(unused)]
#![feature(box_vec_non_null)]
extern crate alloc;
fn main() {
    
    unsafe {
        let mut v = vec![0];
        let ptr1 = v.as_non_null();
        ptr1.write(1);
        let ptr2 = v.as_non_null();
        ptr2.write(2);
        // Notably, the write to `ptr2` did *not* invalidate `ptr1`:
        ptr1.write(3);
    }
}
