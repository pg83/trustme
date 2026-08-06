// Extracted from library/alloc/src/boxed.rs:1520
#![allow(unused)]
#![feature(box_as_ptr)]
extern crate alloc;
fn main() {
    
    unsafe {
        let mut v = Box::new(0);
        let ptr1 = Box::as_ptr(&v);
        let ptr2 = Box::as_mut_ptr(&mut v);
        let _val = ptr2.read();
        // No write to this memory has happened yet, so `ptr1` is still valid.
        let _val = ptr1.read();
        // However, once we do a write...
        ptr2.write(1);
        // ... `ptr1` is no longer valid.
        // This would be UB: let _val = ptr1.read();
    }
}
