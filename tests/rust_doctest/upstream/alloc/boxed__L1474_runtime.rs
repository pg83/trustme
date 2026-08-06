// Extracted from library/alloc/src/boxed.rs:1474
#![allow(unused)]
#![feature(box_as_ptr)]
extern crate alloc;
fn main() {
    
    unsafe {
        let mut b = Box::new(0);
        let ptr1 = Box::as_mut_ptr(&mut b);
        ptr1.write(1);
        let ptr2 = Box::as_mut_ptr(&mut b);
        ptr2.write(2);
        // Notably, the write to `ptr2` did *not* invalidate `ptr1`:
        ptr1.write(3);
    }
}
